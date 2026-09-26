"""What Emacs says to draw for the things a screen is made of.

  python scripts/verify/draw-features.py

Text on a background is most of a screen, and was what the drawing was
first made to carry.  The rest -- what the region looks like, the wave
under a word the checker does not like, a cursor that is a box rather
than a bar, a frame floating over the frame -- each comes out as some
kind of drawing, and one that Emacs never says is one the host is never
asked for and may quietly not do.

So each is put on the screen in turn and what Emacs said for it is
counted by kind.  A row that says nothing is a hole.
"""
import io
import json
import subprocess
import sys
import threading
import time

SETTLE = 22
# Each thing to look at, and what Emacs does to put it on the screen.
STEPS = [
    ('plain text', '(progn (switch-to-buffer (get-buffer-create "look"))'
                   ' (erase-buffer) (insert "plain text here\\n")'
                   ' (goto-char (point-min)))'),
    ('a region', '(progn (goto-char (point-min)) (set-mark (point))'
                 ' (goto-char (line-end-position)) (activate-mark)'
                 ' (setq transient-mark-mode t) (setq deactivate-mark nil))'),
    ('an underline', '(progn (erase-buffer)'
                     ' (insert (propertize "underlined" (quote face)'
                     '                     (quote (:underline t))))'
                     ' (insert "\\n"))'),
    ('a wave', '(progn (erase-buffer)'
               ' (insert (propertize "wavy" (quote face)'
               '                     (quote (:underline (:style wave)))))'
               ' (insert "\\n"))'),
    ('a box around text', '(progn (erase-buffer)'
                          ' (insert (propertize "boxed" (quote face)'
                          '                     (quote (:box t))))'
                          ' (insert "\\n"))'),
    ('a hollow cursor', '(progn (erase-buffer) (insert "cursor\\n")'
                        ' (goto-char (point-min))'
                        ' (setq cursor-type (quote hollow)))'),
    ('a bar cursor', '(setq cursor-type (quote bar))'),
    ('a box cursor', '(setq cursor-type t)'),
    ('a child frame', '(progn (setq cursor-type t)'
                      ' (make-frame (list (quote parent-frame)'
                      '                   (selected-frame)'
                      '                   (quote width) 20'
                      '                   (quote height) 4'
                      '                   (quote left) 100'
                      '                   (quote top) 100)))'),
    ('an image', '(progn (erase-buffer)'
                 ' (insert-image (create-image (make-string 64 0) (quote xbm)'
                 '                             t :width 8 :height 8))'
                 ' (insert "\\n"))'),
]

# Each step runs a little after the one before it, so that what each
# said can be told from what the last one said.
EVERY = 1.5
# Shown and drawn again each time, so that what is being looked at is
# on the screen rather than only in a buffer.
STEP_LISP = ''.join(
    ' (run-at-time %.1f nil (lambda () (condition-case err'
    ' (progn (switch-to-buffer (get-buffer-create "look")) %s (redisplay t))'
    ' (error (princ (format "PROBE %s failed: %%S\\n" err)'
    '                #\'external-debugging-output)))))'
    % (SETTLE - 2 + at * EVERY, lisp, name.replace('"', ''))
    for at, (name, lisp) in enumerate(STEPS))

PROBE = '(progn (setq host-draw-commands t)%s)' % STEP_LISP

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urusi/site-lisp: '
         'exec ~/dev/urusi/host-build/src/emacs '
         '--init-directory ~/dev/urusi/emacs-config/.config/emacs --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + PROBE, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None)

lock = threading.Lock()
seen = []


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
        if message.get('type') == 'draw':
            with lock:
                seen.append((time.monotonic(), message))
        elif message.get('type') == 'call':
            send({'type': 'reply', 'id': message['id'], 'value': None})


threading.Thread(target=read_out, daemon=True).start()
time.sleep(SETTLE - 3)
send({'type': 'resize', 'width': 800, 'height': 600})
time.sleep(1)

began = time.monotonic()
with lock:
    del seen[:]

marks = []
for at, (name, _) in enumerate(STEPS):
    time.sleep(EVERY)
    marks.append((name, time.monotonic()))
time.sleep(1.5)
emacs.kill()

was = began
for name, until in marks:
    with lock:
        window = [message for when, message in seen if was < when <= until]
    was = until

    kinds = {}
    frames = set()
    for message in window:
        kinds[message.get('op')] = kinds.get(message.get('op'), 0) + 1
        if message.get('op') == 'begin':
            frames.add(message.get('frame'))

    said = ', '.join('%s %d' % (op, count)
                     for op, count in sorted(kinds.items(), key=lambda p: -p[1])
                     if op not in ('begin', 'end'))
    print('%-20s %s' % (name, said or 'NOTHING'))
    if len(frames) > 1:
        print('%-20s   on %d frames: %s' % ('', len(frames), ', '.join(sorted(frames))))
