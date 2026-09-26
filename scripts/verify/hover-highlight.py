"""What Emacs does when the pointer comes to rest on text marked to light
up under it, with no host of its own.

  python scripts/verify/hover-highlight.py            # Emacs alone
  python scripts/verify/hover-highlight.py --config   # with the init file

The Emacs of the host window system is started over a pipe, as the host
starts it, with a line that carries a `mouse-face' on part of it. Emacs
itself says where on the screen that part is; the pointer is sent there,
and what Emacs makes of it is read from its traces, beside the pictures
it hands over. That tells a highlight that was never worked out from one
that was worked out and never drawn.

With --config the whole init file is loaded first, as the host loads it,
to tell a setting of the user's own from the port itself.
"""
import json
import subprocess
import sys
import threading
import time

WITH_CONFIG = '--config' in sys.argv
SETTLE = 25 if WITH_CONFIG else 6

SETUP = r"""(progn
  (switch-to-buffer (get-buffer-create "hover"))
  (erase-buffer)
  (insert "a plain line\n")
  (insert "link: ")
  (let ((start (point)))
    (insert (propertize "under the pointer" 'mouse-face 'highlight
                        'help-echo "a file name"))
    (insert " and plain again\n")
    (goto-char (point-min))
    (run-at-time
     SETTLE nil
     (lambda ()
       (switch-to-buffer (get-buffer-create "hover"))
       (redisplay t)
       (let* ((posn (posn-at-point start))
              (xy (and posn (posn-x-y posn)))
              (edges (window-inside-pixel-edges)))
         (princ (format "PROBE mouse-face at %d, screen %S, edges %S, frame %dx%d, highlight %S\n"
                        start xy edges
                        (frame-pixel-width) (frame-pixel-height)
                        mouse-highlight)
                #'external-debugging-output))))))""".replace(
    'SETTLE', str(SETTLE - 3))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urusi/site-lisp: '
         'exec ~/dev/urusi/host-build/src/emacs'
         + (' --init-directory ~/dev/urusi/emacs-config/.config/emacs' if WITH_CONFIG
            else ' -Q')
         + ' --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + SETUP, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

lock = threading.Lock()
said = []
pictures = [0]


def send(message):
    with lock:
        emacs.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        emacs.stdin.flush()


def read_out():
    for raw in emacs.stdout:
        try:
            message = json.loads(raw.decode('utf-8', 'replace'))
        except ValueError:
            continue
        kind = message.get('type')
        if kind == 'picture':
            pictures[0] += 1
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})
        elif kind == 'want-font':
            pass


def read_err():
    for raw in emacs.stderr:
        line = raw.decode('utf-8', 'replace').rstrip()
        if 'TRACE' in line or 'PROBE' in line:
            said.append(line)
            print(' ', line)


threading.Thread(target=read_out, daemon=True).start()
threading.Thread(target=read_err, daemon=True).start()

print('--- starting, waiting for where the mouse-face is')
time.sleep(SETTLE)

probe = next((line for line in said if 'PROBE' in line), None)
if not probe:
    print('no probe: Emacs never drew the line')
    emacs.kill()
    sys.exit(1)

# "screen (X . Y), edges (L T R B)"
screen = probe.split('screen (')[1].split(')')[0]
x, y = (int(part.strip()) for part in screen.split('.'))
edges = probe.split('edges (')[1].split(')')[0].split()
x += int(edges[0])
y += int(edges[1]) + 8

print('--- the pointer arrives at %d,%d' % (x + 40, y))
pictures[0] = 0
send({'type': 'pointer', 'kind': 'move', 'x': x + 40, 'y': y, 'modifiers': []})
time.sleep(2)
print('   pictures handed over:', pictures[0])

print('--- and moves away again')
pictures[0] = 0
send({'type': 'pointer', 'kind': 'move', 'x': x + 40, 'y': y + 60, 'modifiers': []})
time.sleep(2)
print('   pictures handed over:', pictures[0])

# The one that matters: the first hover is followed by an update of its
# own, and an update tells Emacs to leave the highlight alone until the
# port says the update is over. Coming back has to light up again.
print('--- and comes back to it, after all that drawing')
pictures[0] = 0
send({'type': 'pointer', 'kind': 'move', 'x': x + 60, 'y': y, 'modifiers': []})
time.sleep(2)
print('   pictures handed over:', pictures[0])

emacs.kill()
