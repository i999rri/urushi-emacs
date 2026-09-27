"""How far the line hl-line-mode marks is coloured, when its text does
not begin at the window's edge.

  python scripts/verify/hl-line-align.py 1     the line padded with spaces
  python scripts/verify/hl-line-align.py 2     the line aligned by a display
  python scripts/verify/hl-line-align.py 3     a line long enough to wrap
  python scripts/verify/hl-line-align.py 3     the line whose blank has a face

Two lines are made: one padded with spaces, and one whose blank is a
`display' of (space :align-to), which is how a dashboard centres its
text. The cursor is put on the line asked for and the backgrounds of
every row are printed. It says whether Emacs colours the blank before
the text, so that a window which does not can be told from one that is
drawing what Emacs decided.
"""
import json
import os
import re
import subprocess
import sys
import threading
import time

LINE = sys.argv[1] if len(sys.argv) > 1 else '1'
SETUP = """(progn
  (switch-to-buffer (get-buffer-create "aligned"))
  (insert "    spaces before the text\\n")
  (insert (propertize " " (quote display) (quote (space :align-to 20)))
          "display before the text\\n")
  (insert (make-string 300 ?x) "\n")
  (goto-char (point-min))
  (forward-line %s)
  (hl-line-mode 1))""" % (int(LINE) - 1)

# This repository's Lisp, named as WSL sees it and worked out from where
# this file is: -Q leaves the site file unread, so what would have loaded
# urusi is named here instead.
LISP = '/mnt/%s%s' % (os.path.abspath(__file__)[0].lower(),
                      os.path.join(os.path.dirname(os.path.dirname(
                          os.path.dirname(os.path.abspath(__file__)))),
                                   'lisp')[2:].replace('\\', '/'))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urusi/site-lisp: '
         'exec ~/dev/urusi/emacs-build/src/emacs -Q '
         '-L %s -l %s/urusi-site-start.el --eval "$PROBE"' % (LISP, LISP))
emacs = subprocess.Popen(['wsl.exe', '-e', 'env', 'PROBE=' + SETUP, 'bash', '-lc', INNER],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
lock = threading.Lock()
rows = {}


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
            send({'type': 'resize', 'width': 1000, 'height': 400})
        elif kind == 'measure':
            send({'type': 'measured', 'family': message['family'], 'size': message['size'],
                  'narrow': 9.0, 'wide': 18.0})
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})
        elif kind == 'screen':
            for group in message.get('rows', []):
                if group.get('panel') != 'window-0':
                    continue
                for item in group.get('items', []):
                    if 'xaml' in item:
                        rows[item['key']] = item['xaml']


threading.Thread(target=read, daemon=True).start()
time.sleep(8)
emacs.kill()

print('cursor on line', LINE)
for key, xaml in sorted(rows.items()):
    pieces = re.findall(
        r'Canvas.Left="([0-9.]*)" Width="([0-9.]*)"[^>]*Background="(#[0-9a-f]*)"', xaml)
    text = ''.join(re.findall(r'<TextBlock Text="([^"]*)"', xaml)).strip()
    if text and 'U:' not in text:
        print(' ', pieces, repr(text[:34]))
