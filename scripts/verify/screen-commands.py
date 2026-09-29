"""What a whole screen comes to, said as commands.

  python scripts/verify/screen-commands.py [--file PATH] [--show 12]

A file is put on the screen, the frame is sized again to have all of it
drawn afresh, and what Emacs says to draw is counted by kind. A screen
of text should come to about as many glyphs as it has characters on it;
far fewer means something was drawn and not said.
"""
import json
import subprocess
import sys
import threading
import time

FILE = (sys.argv[sys.argv.index('--file') + 1] if '--file' in sys.argv
        else '~/dev/urushi/emacs/src/keyboard.c')
SHOW = int(sys.argv[sys.argv.index('--show') + 1]) if '--show' in sys.argv else 0
SETTLE = 22

PROBE = ('(progn (setq host-draw-commands t)'
         ' (run-at-time %d nil'
         '  (lambda () (switch-to-buffer (find-file-noselect "%s"))'
         '             (goto-char (point-min)) (redisplay t)'
         '             (princ (format "PROBE showing %%s, window %%d lines,'
         ' %%d columns\\n"'
         '                            (buffer-name) (window-body-height)'
         '                            (window-body-width))'
         '                    #\'external-debugging-output))))'
         % (SETTLE - 2, FILE))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: '
         'exec ~/dev/urushi/emacs-build/src/emacs '
         '--init-directory ~/dev/urushi/emacs-config/.config/emacs --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + PROBE, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None)

lock = threading.Lock()
frames = []
current = [None]


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
        if kind == 'draw':
            op = message.get('op')
            if op == 'begin':
                current[0] = {'bytes': len(raw), 'commands': []}
            elif current[0] is None:
                continue
            elif op == 'end':
                current[0]['bytes'] += len(raw)
                frames.append(current[0])
                current[0] = None
            else:
                current[0]['bytes'] += len(raw)
                current[0]['commands'].append(message)
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


threading.Thread(target=read_out, daemon=True).start()
time.sleep(SETTLE)

# Sized again, so that the whole of the screen is drawn afresh rather
# than whatever last changed on it.
del frames[:]
send({'type': 'resize', 'width': 1196, 'height': 800})
time.sleep(3)
emacs.kill()

if not frames:
    sys.exit('nothing was drawn')

biggest = max(frames, key=lambda frame: len(frame['commands']))
kinds = {}
glyphs = 0
for command in biggest['commands']:
    kinds[command['op']] = kinds.get(command['op'], 0) + 1
    if command['op'] == 'glyphs':
        glyphs += len(command['ids'])

print('%d frames; the fullest has %d commands in %.1f KB'
      % (len(frames), len(biggest['commands']), biggest['bytes'] / 1e3))
for op, count in sorted(kinds.items(), key=lambda pair: -pair[1]):
    print('  %-10s %d' % (op, count))
print('  %d glyphs in all' % glyphs)

for command in biggest['commands'][:SHOW]:
    print('   ', json.dumps(command, ensure_ascii=False)[:160])
