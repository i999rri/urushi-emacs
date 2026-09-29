"""What the screen comes to while the pointer is over text that is
marked to light up under it.

  python scripts/verify/mouse-face.py

A line is made with a `mouse-face' on part of it, the pointer is moved
on to that part, and the backgrounds of the row are printed. Redisplay
does not put the mouse face in the glyph matrix -- it draws the glyphs
again with it and puts the old ones back -- so a window drawing from the
matrix sees it only where hostscreen.c reads it back.
"""
import json
import os
import re
import subprocess
import threading
import time

SETUP = r"""(progn
  (switch-to-buffer (get-buffer-create "hover"))
  (insert "plain ")
  (insert (propertize "under the pointer"
                      'mouse-face 'highlight
                      'help-echo "a file name"))
  (insert " plain\n")
  (goto-char (point-min)))"""

# This repository's Lisp, named as WSL sees it and worked out from where
# this file is: -Q leaves the site file unread, so what would have loaded
# urushi is named here instead.
LISP = '/mnt/%s%s' % (os.path.abspath(__file__)[0].lower(),
                      os.path.join(os.path.dirname(os.path.dirname(
                          os.path.dirname(os.path.abspath(__file__)))),
                                   'lisp')[2:].replace('\\', '/'))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: '
         'exec ~/dev/urushi/emacs-build/src/emacs -Q '
         '-L %s -l %s/urushi-site-start.el --eval "$PROBE"' % (LISP, LISP))
emacs = subprocess.Popen(['wsl.exe', '-e', 'env', 'PROBE=' + SETUP, 'bash', '-lc', INNER],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
lock = threading.Lock()
rows = {}
screens = [0]


def send(message):
    with lock:
        emacs.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        emacs.stdin.flush()


def show(title):
    print('-----', title, 'screens:', screens[0], 'keys:', sorted(rows))
    screens[0] = 0
    for key, xaml in sorted(rows.items()):
        pieces = re.findall(
            r'Canvas.Left="([0-9.]*)" Width="([0-9.]*)"[^>]*Background="(#[0-9a-f]*)"', xaml)
        text = ''.join(re.findall(r'<TextBlock Text="([^"]*)"', xaml)).strip()
        if text and 'U:' not in text:
            print(' ', pieces, repr(text[:40]))


def read():
    for raw in emacs.stdout:
        message = json.loads(raw.decode('utf-8', 'replace'))
        kind = message.get('type')
        if kind == 'hello':
            send({'type': 'hello', 'host': 'verify', 'version': 1, 'scale': 1.0,
                  'debug': False})
            send({'type': 'resize', 'width': 1000, 'height': 400})
        elif kind == 'measure':
            send({'type': 'measured', 'family': message['family'], 'size': message['size'],
                  'narrow': 9.0, 'wide': 18.0})
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})
        elif kind == 'screen':
            screens[0] += 1
            for group in message.get('rows', []):
                if group.get('panel') != 'window-0':
                    continue
                for item in group.get('items', []):
                    if 'xaml' in item:
                        rows[item['key']] = item['xaml']


threading.Thread(target=read, daemon=True).start()
time.sleep(6)
show('the pointer away from it')

# Over the tenth column of the first line, which is inside the part
# that is marked.
rows.clear()
send({'type': 'text', 'text': '!'})
time.sleep(2)
show('after typing, to show the same road works')

rows.clear()
send({'type': 'pointer', 'kind': 'down', 'button': 1, 'clicks': 1,
      'x': 150, 'y': 10, 'modifiers': []})
send({'type': 'pointer', 'kind': 'up', 'button': 1, 'clicks': 1,
      'x': 150, 'y': 10, 'modifiers': []})
time.sleep(2)
show('after a click there, to show the place is right')

rows.clear()
send({'type': 'pointer', 'kind': 'move', 'x': 150, 'y': 10, 'modifiers': []})
time.sleep(2)
show('the pointer over it')

# Draw again without touching the buffer, to tell a highlight that was
# never worked out from one that was and never sent.
rows.clear()
send({'type': 'resize', 'width': 1000, 'height': 400})
time.sleep(2)
show('and drawn again')
emacs.kill()
