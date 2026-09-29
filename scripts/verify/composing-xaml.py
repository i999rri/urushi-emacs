"""What the screen comes to while the input method is composing.

  python scripts/verify/composing-xaml.py

Starts the Emacs of ~/.urushi-emacs-remote as the application starts it,
says what the host would say, and then sends one composition of two
marked stretches. Prints the XAML of the cursor canvas, which is where
what is being composed is drawn: each stretch stands where Emacs would
put the text, spaced to Emacs's grid, with a line of its own kind under
it and the caret where the input method left it.

Written for the move to the text services, where each of those was got
wrong once and none of them can be seen from a test.
"""
import json
import os
import re
import subprocess
import threading
import time

EVAL = ('(run-at-time 1 nil (lambda () (switch-to-buffer (get-buffer-create "typing"))'
        ' (text-mode)))')
INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: '
         'exec ~/dev/urushi/emacs-build/src/emacs '
         '--init-directory ~/dev/urushi/emacs-config/.config/emacs --eval "$PROBE"')

emacs = subprocess.Popen(['wsl.exe', '-e', 'env', 'PROBE=' + EVAL, 'bash', '-lc', INNER],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
lock = threading.Lock()
drawn = {}


def send(message):
    with lock:
        emacs.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        emacs.stdin.flush()


def read():
    for raw in emacs.stdout:
        message = json.loads(raw.decode('utf-8', 'replace'))
        kind = message.get('type')
        if kind == 'hello':
            send({'type': 'hello', 'host': 'verify', 'version': 1, 'scale': 1.0,
                  'debug': False})
            send({'type': 'resize', 'width': 1200, 'height': 600})
        elif kind == 'measure':
            # What a host that drew in this font would answer: Emacs
            # keeps a grid of whole columns and the font does not.
            send({'type': 'measured', 'family': message['family'], 'size': message['size'],
                  'narrow': 9.0, 'wide': 18.0})
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})
        elif kind == 'screen':
            for group in message.get('rows', []):
                for item in group.get('items', []):
                    if 'xaml' in item and 'cursor' in str(item.get('key')):
                        drawn['cursor'] = item['xaml']


threading.Thread(target=read, daemon=True).start()
time.sleep(25)
send({'type': 'composition', 'text': 'あいうえおかきくけこ', 'caret': 4,
      'runs': [{'length': 4, 'underline': 'double'},
               {'length': 6, 'underline': 'dotted'}]})
time.sleep(3)
emacs.kill()

for element in re.findall(r'<[A-Za-z]+[^>]*/?>', drawn.get('cursor', '')):
    print(element)
