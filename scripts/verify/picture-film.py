"""The pictures Emacs hands the host, one PNG apiece.

  python scripts/verify/picture-film.py --config --click

Emacs is started over a pipe as the host starts it, and every picture it
posts is put into a canvas of its own and written out, numbered in the
order they came. What the screen looked like for a moment -- between a
click and what the click led to, say -- is then a file to look at rather
than something to catch by eye.

Without --config Emacs is started with -Q and a line of its own carrying
a `mouse-face'; with it the whole init file is loaded and the first
`mouse-face' in the buffer that comes up is what is pointed at. --click
presses there as well as pointing.
"""
import base64
import json
import os
import struct
import subprocess
import sys
import threading
import time
import zlib

WITH_CONFIG = '--config' in sys.argv
CLICK = '--click' in sys.argv
SETTLE = 22 if WITH_CONFIG else 6
OUT = os.path.join(os.environ.get('TEMP', '.'), 'picture-film')

PROBE = r"""(run-at-time
 SETTLE nil
 (lambda ()
   (switch-to-buffer (or (get-buffer "*dashboard*") (get-buffer-create "hover")))
   (redisplay t)
   (let ((at (next-single-char-property-change (point-min) 'mouse-face)))
     (when (and at (< at (point-max)))
       (let ((posn (posn-at-point at)))
         (princ (format "PROBE mouse-face at %d, screen %S, edges %S\n"
                        at (and posn (posn-x-y posn))
                        (window-inside-pixel-edges))
                #'external-debugging-output))))))"""

# A button rather than a bare `mouse-face': what is being looked at is
# what the screen does between the click and what the click leads to, so
# the click has to lead somewhere.
SETUP = r"""(progn
  (switch-to-buffer (get-buffer-create "hover"))
  (erase-buffer)
  (insert "a plain line\n")
  (insert "link: ")
  (insert-text-button "under the pointer"
                      'action (lambda (_) (switch-to-buffer "*Messages*")))
  (insert " and plain again\n")
  (goto-char (point-min))
  PROBE)"""

form = SETUP.replace('PROBE', PROBE).replace('SETTLE', str(SETTLE - 3))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: '
         'exec ~/dev/urushi/emacs-build/src/emacs'
         + (' --init-directory ~/dev/urushi/emacs-config/.config/emacs' if WITH_CONFIG
            else ' -Q')
         + ' --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + form, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

lock = threading.Lock()
said = []
canvas = {}
shots = [0]
taking = [False]


def send(message):
    with lock:
        emacs.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        emacs.stdin.flush()


def write_png(path, width, height, bgra):
    rgb = bytearray(width * height * 3)
    rgb[0::3] = bgra[2::4]
    rgb[1::3] = bgra[1::4]
    rgb[2::3] = bgra[0::4]
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        head = struct.pack('>I', len(body)) + kind
        return head + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    with open(path, 'wb') as out:
        out.write(b'\x89PNG\r\n\x1a\n')
        out.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        out.write(chunk(b'IDAT', zlib.compress(bytes(raw), 6)))
        out.write(chunk(b'IEND', b''))


def picture(message):
    name = message.get('frame', '')
    width, height = message['width'], message['height']
    state = canvas.get(name)
    if not state or state[0] != width or state[1] != height:
        state = (width, height, bytearray(width * height * 4))
        canvas[name] = state
    _, _, cells = state
    stride = width * 4

    for move in message.get('moved', []):
        x, y, w, h, to = (move['x'], move['y'], move['width'], move['height'],
                          move['toY'])
        rows = [cells[(y + i) * stride + x * 4:(y + i) * stride + (x + w) * 4]
                for i in range(h)]
        for i, row in enumerate(rows):
            cells[(to + i) * stride + x * 4:(to + i) * stride + (x + w) * 4] = row

    boxes = message.get('drawn', [])
    for box in boxes:
        x, y, w, h = box['x'], box['y'], box['width'], box['height']
        raw = base64.b64decode(box['cells'])
        for i in range(h):
            cells[(y + i) * stride + x * 4:(y + i) * stride + (x + w) * 4] \
                = raw[i * w * 4:(i + 1) * w * 4]

    if taking[0]:
        shots[0] += 1
        path = os.path.join(OUT, 'film-%03d.png' % shots[0])
        write_png(path, width, height, cells)
        print('  %s  %d moved, %d drawn %s'
              % (os.path.basename(path), len(message.get('moved', [])), len(boxes),
                 [(b['x'], b['y'], b['width'], b['height']) for b in boxes[:4]]))


def read_out():
    for raw in emacs.stdout:
        try:
            message = json.loads(raw.decode('utf-8', 'replace'))
        except ValueError:
            continue
        kind = message.get('type')
        if kind == 'picture':
            picture(message)
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


def read_err():
    for raw in emacs.stderr:
        line = raw.decode('utf-8', 'replace').rstrip()
        if 'PROBE' in line:
            said.append(line)
            print(' ', line)


os.makedirs(OUT, exist_ok=True)
for stale in os.listdir(OUT):
    os.remove(os.path.join(OUT, stale))

threading.Thread(target=read_out, daemon=True).start()
threading.Thread(target=read_err, daemon=True).start()

print('--- starting; pictures go to', OUT)
time.sleep(SETTLE)

probe = next((line for line in said if 'PROBE' in line), None)
if not probe:
    print('no probe: nothing in the buffer carries a mouse-face')
    emacs.kill()
    sys.exit(1)

screen = probe.split('screen (')[1].split(')')[0]
x, y = (int(part.strip()) for part in screen.split('.'))
edges = probe.split('edges (')[1].split(')')[0].split()
x += int(edges[0]) + 20
y += int(edges[1]) + 8

taking[0] = True
print('--- a picture of the screen as it stands, drawn again whole')
send({'type': 'resize', 'width': 1196, 'height': 800})
time.sleep(2)

print('--- the pointer arrives at %d,%d' % (x, y))
send({'type': 'pointer', 'kind': 'move', 'x': x, 'y': y, 'modifiers': []})
time.sleep(2)

if CLICK:
    print('--- and presses there')
    send({'type': 'pointer', 'kind': 'down', 'button': 1, 'clicks': 1,
          'x': x, 'y': y, 'modifiers': []})
    time.sleep(0.4)
    send({'type': 'pointer', 'kind': 'up', 'button': 1, 'clicks': 1,
          'x': x, 'y': y, 'modifiers': []})
    time.sleep(4)

print('---', shots[0], 'pictures')
emacs.kill()
