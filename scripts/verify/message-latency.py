"""How long a message of the host's waits before Lisp sees it.

  python scripts/verify/message-latency.py            # told, as it comes
  python scripts/verify/message-latency.py --poll     # looked for on a timer

A message the host sends that is not input goes to a queue of Lisp's.
Emacs is told there is one, and handles it from the command loop; with
--poll that is turned off and the old timer put back, which is what
Emacs does where it cannot be told.

Each round is a message Lisp answers by posting one back, so what is
measured is the whole way there and back.
"""
import json
import statistics
import subprocess
import sys
import threading
import time

POLL = '--poll' in sys.argv
ROUNDS = 20

ANSWER = r"""(progn
  (add-hook 'urusi-message-hook
            (lambda (message)
              (when (equal (plist-get message :type) "ping")
                (host-post (json-serialize
                            (list :type "log"
                                  :text (format "pong %s"
                                                (plist-get message :id))))))))
  PUT-BACK-THE-TIMER)"""

TIMER = r"""(progn (setq host-message-function nil)
         (setq urusi--timer
               (run-with-timer urusi-poll-interval urusi-poll-interval
                               #'urusi--take)))"""

form = ANSWER.replace('PUT-BACK-THE-TIMER', TIMER if POLL else '(ignore)')

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urusi/site-lisp: '
         'exec ~/dev/urusi/host-build/src/emacs '
         '--init-directory ~/dev/urusi/emacs-config/.config/emacs --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + form, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

lock = threading.Lock()
came = {}


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
        if message.get('type') == 'log' and message.get('text', '').startswith('pong '):
            came[int(message['text'].split()[1])] = time.monotonic()
        elif message.get('type') == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


threading.Thread(target=read_out, daemon=True).start()
time.sleep(25)

waits = []
for round in range(ROUNDS):
    began = time.monotonic()
    send({'type': 'ping', 'id': round})
    while round not in came and time.monotonic() - began < 2:
        time.sleep(0.001)
    if round in came:
        waits.append((came[round] - began) * 1000)
    time.sleep(0.2)

emacs.kill()
if not waits:
    sys.exit('no answer came back')
print('%s: %d of %d answered, %.1f ms on average, %.1f ms at worst'
      % ('looked for on a timer' if POLL else 'told as it comes',
         len(waits), ROUNDS, statistics.mean(waits), max(waits)))
