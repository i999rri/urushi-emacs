"""The XAML Lisp builds the window out of, as the host is sent it.

  python scripts/verify/chrome-xaml.py

Starts the Emacs of ~/.urushi-emacs-remote as the application starts it
and prints the chrome of the first screen it sends: the elements the
rows are put into, and what else is around and over them. It says where
the element the pointer is read from stands in the tree.
"""
import json
import os
import re
import subprocess
import threading
import time

path = os.path.expanduser('~/.urushi-emacs-remote')
command = next(line.strip() for line in open(path, encoding='utf-8')
               if line.strip() and not line.strip().startswith('#'))
emacs = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
lock = threading.Lock()
chrome = []


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
            send({'type': 'resize', 'width': 1200, 'height': 700})
        elif kind == 'measure':
            send({'type': 'measured', 'family': message['family'], 'size': message['size'],
                  'narrow': 9.0, 'wide': 18.0})
        elif kind == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})
        elif kind == 'screen' and 'xaml' in message:
            chrome.append(message['xaml'])


threading.Thread(target=read, daemon=True).start()
time.sleep(28)
emacs.kill()

if not chrome:
    raise SystemExit('no chrome was sent')

# Print it as a tree of the elements that carry a name or a background,
# which is what decides where the pointer lands.
depth = 0
for piece in re.findall(r'<[^>]+>', chrome[-1]):
    if piece.startswith('</'):
        depth -= 1
        continue
    kept = re.findall(r'(Name|Background|Canvas\.ZIndex|Width|Height)="([^"]*)"', piece)
    tag = re.match(r'<([A-Za-z.]+)', piece).group(1)
    print('  ' * depth + tag + ''.join(f' {k}={v}' for k, v in kept))
    if not piece.endswith('/>'):
        depth += 1
