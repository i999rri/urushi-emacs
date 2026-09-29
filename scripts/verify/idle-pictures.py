"""What Emacs hands the host while nothing is happening.

  python scripts/verify/idle-pictures.py [--config] [--seconds 10]

Emacs is started over a pipe and then left alone. Every picture it posts
is counted by the boxes it carries, so that a screen that is being drawn
again and again while it stands still says which part of it is doing the
drawing.

The host here answers every call with nothing, which is not what a host
does: Lisp then builds the screen again on the next look, and the count
is of that as much as of anything. Read it beside scripts/verify/
idle-cost.sh, which reads the time the process actually spent.
"""
import collections
import json
import subprocess
import sys
import threading
import time

WITH_CONFIG = '--config' in sys.argv
SECONDS = int(sys.argv[sys.argv.index('--seconds') + 1]) if '--seconds' in sys.argv else 10
SETTLE = 22 if WITH_CONFIG else 6

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: '
         'exec ~/dev/urushi/emacs-build/src/emacs'
         + (' --init-directory ~/dev/urushi/emacs-config/.config/emacs' if WITH_CONFIG
            else ' -Q'))

emacs = subprocess.Popen(['wsl.exe', '-d', 'NixOS', '-e', 'bash', '-lc', INNER],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)

lock = threading.Lock()
counting = [False]
seen = collections.Counter()
total = [0]


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
            total[0] += 1
            for box in message.get('drawn', []):
                seen[(box['x'], box['y'], box['width'], box['height'])] += 1
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


threading.Thread(target=read_out, daemon=True).start()
time.sleep(SETTLE)
send({'type': 'resize', 'width': 1196, 'height': 800})
time.sleep(2)

counting[0] = True
began = time.monotonic()
time.sleep(SECONDS)
counting[0] = False
took = time.monotonic() - began

print('%d pictures in %.1fs -- %.1f a second' % (total[0], took, total[0] / took))
for box, count in seen.most_common(8):
    print('  %-24s %4d  (%.1f a second)' % ('x%d y%d %dx%d' % box, count, count / took))
emacs.kill()
