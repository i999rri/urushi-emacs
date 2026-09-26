"""What a scroll costs on the wire.

  python scripts/verify/scroll-traffic.py [--seconds 10] [--file PATH]

Emacs is started over a pipe as the host starts it, a file is opened in
it, and the wheel is turned at the rate a hand turns it. Every picture
Emacs hands back is counted -- how many, how many bytes, and how much of
that is pixels rather than the rest of the message -- so that a screen
that costs too much says whether it is sending too often or sending too
much each time.
"""
import base64
import json
import subprocess
import sys
import threading
import time

SECONDS = int(sys.argv[sys.argv.index('--seconds') + 1]) if '--seconds' in sys.argv else 10
FILE = (sys.argv[sys.argv.index('--file') + 1] if '--file' in sys.argv
        else '~/dev/urusi/emacs-config/.config/emacs/asiimov-theme.el')
PLAIN = '--plain' in sys.argv
SETTLE = 6 if PLAIN else 22
# A wheel notch every 60ms, which is about as fast as a hand turns it.
STEP = 0.06

# The file is put on the screen once everything the init file starts has
# settled, since some of that shows a screen of its own; and where the
# top of the window is by the end says the wheel moved something.
PROBE = ('(progn'
         ' (run-at-time %d nil'
         '  (lambda () (switch-to-buffer (find-file-noselect "%s"))'
         '             (goto-char (point-min)) (redisplay t)'
         '             (princ (format "PROBE showing %%s, %%d characters\\n"'
         '                            (buffer-name) (point-max))'
         '                    #\'external-debugging-output)))'
         ' (run-at-time %d nil'
         '  (lambda () (princ (format "PROBE %%s starts at %%d of %%d\\n"'
         '                            (buffer-name) (window-start) (point-max))'
         '                    #\'external-debugging-output))))'
         % (SETTLE - 2, FILE, SETTLE + SECONDS + 1))

INNER = ('EMACS_HOST_PIPE=1 '
         + ('' if PLAIN else 'EMACSLOADPATH=$HOME/dev/urusi/site-lisp: ')
         + 'exec ~/dev/urusi/host-build/src/emacs '
         + ('-Q' if PLAIN
            else '--init-directory ~/dev/urusi/emacs-config/.config/emacs')
         + ' --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + PROBE, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None)

lock = threading.Lock()
counting = [False]
tally = {'pictures': 0, 'bytes': 0, 'moved': 0, 'boxes': 0, 'pixels': 0,
         'widest': 0, 'tallest': 0}


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
        if kind == 'picture' and counting[0]:
            tally['pictures'] += 1
            tally['bytes'] += len(raw)
            tally['moved'] += len(message.get('moved', []))
            for box in message.get('drawn', []):
                tally['boxes'] += 1
                tally['pixels'] += box['width'] * box['height']
                tally['widest'] = max(tally['widest'], box['width'])
                tally['tallest'] = max(tally['tallest'], box['height'])
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


threading.Thread(target=read_out, daemon=True).start()
time.sleep(SETTLE)
send({'type': 'resize', 'width': 1196, 'height': 800})
time.sleep(2)

counting[0] = True
began = time.monotonic()
while time.monotonic() - began < SECONDS:
    send({'type': 'pointer', 'kind': 'wheel', 'x': 600, 'y': 400,
          'dx': 0, 'dy': -3, 'modifiers': []})
    time.sleep(STEP)
time.sleep(1)
counting[0] = False
took = time.monotonic() - began
emacs.kill()

notches = int(SECONDS / STEP)
print('%d notches of the wheel in %.1fs' % (notches, took))
print('  %d pictures -- %.1f a second, %.1f a notch'
      % (tally['pictures'], tally['pictures'] / took,
         tally['pictures'] / max(notches, 1)))
print('  %.1f MB -- %.2f MB a second, %.1f KB a picture'
      % (tally['bytes'] / 1e6, tally['bytes'] / took / 1e6,
         tally['bytes'] / max(tally['pictures'], 1) / 1e3))
print('  %d moved boxes, %d drawn boxes, %.1f million pixels'
      % (tally['moved'], tally['boxes'], tally['pixels'] / 1e6))
print('  the widest box was %d, the tallest %d'
      % (tally['widest'], tally['tallest']))
print('  pixels are %.0f%% of the bytes (4 bytes each, base64 is 4/3 of that)'
      % (tally['pixels'] * 4 * 4 / 3 / max(tally['bytes'], 1) * 100))
