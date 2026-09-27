"""That what Emacs says to draw is what the host reads.

  python scripts/verify/draw-lines.py [--file PATH] [--record DIR]

The host reads the lines of a screen as they come, without building an
object for each of them, since building one cost as much as the drawing
did. That reader is written by hand, so what it reads has to be checked
against a reader that was not: every line Emacs sends is read here by
Python's own, which is where a line that is not the JSON the host takes
it for would show.

With --record a screen is written down beside what Python read of it,
for tests/windows/Emacs/test_draw_reader_same.cpp to hold the host's
reader to without Emacs having to run.

Then the shape is checked -- that every line is a kind of drawing the
host knows, with the fields that kind needs, of the types it reads them
as -- so that a line Emacs learns to send and the host cannot read is
this failing rather than a screen with a hole in it.
"""
import io
import json
import subprocess
import sys
import threading
import time

FILE = (sys.argv[sys.argv.index('--file') + 1] if '--file' in sys.argv
        else '~/dev/urusi/emacs/src/keyboard.c')
RECORD = (sys.argv[sys.argv.index('--record') + 1] if '--record' in sys.argv
          else None)
SETTLE = 22

# What each kind of drawing says, and what the host reads it as.
SHAPES = {
    'begin': {'frame': str, 'width': int, 'height': int},
    'end': {'frame': str},
    'fill': {'x': int, 'y': int, 'width': int, 'height': int, 'color': str},
    'rectangle': {'x': int, 'y': int, 'width': int, 'height': int,
                  'color': str},
    'line': {'x0': int, 'y0': int, 'x1': int, 'y1': int, 'color': str},
    'copy': {'x': int, 'y': int, 'width': int, 'height': int, 'toY': int},
    'clip': {'x': int, 'y': int, 'width': int, 'height': int},
    'unclip': {},
    'glyphs': {'font': int, 'size': float, 'y': int, 'color': str,
               'ids': list, 'xs': list},
}

PROBE = ('(progn (setq host-draw-commands t)'
         ' (run-at-time %d nil'
         '  (lambda () (switch-to-buffer (find-file-noselect "%s"))'
         '             (goto-char (point-min)) (redisplay t))))'
         % (SETTLE - 2, FILE))

INNER = ('EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urusi/site-lisp: '
         'exec ~/dev/urusi/emacs-build/src/emacs '
         '--init-directory ~/dev/urusi/emacs-config/.config/emacs --eval "$PROBE"')

emacs = subprocess.Popen(
    ['wsl.exe', '-d', 'NixOS', '-e', 'env', 'PROBE=' + PROBE, 'bash', '-lc', INNER],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

lock = threading.Lock()
lines = []
wrong = []


def send(message):
    with lock:
        emacs.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        emacs.stdin.flush()


def read_out():
    for raw in emacs.stdout:
        text = raw.decode('utf-8', 'replace').rstrip('\r\n')
        if not text:
            continue

        # Read the way a reader that was not written by hand reads it.
        try:
            message = json.loads(text)
        except ValueError as why:
            wrong.append('not JSON at all (%s): %s' % (why, text[:120]))
            continue

        if message.get('type') != 'draw':
            if message.get('type') == 'call':
                send({'type': 'reply', 'id': message['id'], 'value': None})
            continue

        lines.append(message)


def numbers(message, name, kind):
    """Whether every element of the array NAME is a whole number."""
    for value in message.get(name, []):
        if not isinstance(value, int) or isinstance(value, bool):
            return 'a %s that is no whole number: %r' % (name, value)
    return None


def check(message):
    op = message.get('op')

    if op not in SHAPES:
        return 'a kind of drawing the host does not know: %r' % (op,)

    for name, kind in SHAPES[op].items():
        if name not in message:
            return 'a %s with no %s' % (op, name)

        value = message[name]
        if kind is float:
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                return 'a %s whose %s is %r' % (op, name, value)
        elif kind is int:
            if not isinstance(value, int) or isinstance(value, bool):
                return 'a %s whose %s is %r' % (op, name, value)
        elif not isinstance(value, kind):
            return 'a %s whose %s is %r' % (op, name, value)

    if op == 'glyphs':
        for name in ('ids', 'xs'):
            if why := numbers(message, name, int):
                return why
        if len(message['ids']) != len(message['xs']):
            return ('a run of %d glyphs with %d places'
                    % (len(message['ids']), len(message['xs'])))
        if message['color'][:1] != '#' or len(message['color']) != 7:
            return 'a color that is no color: %r' % (message['color'],)

    if 'color' in SHAPES[op]:
        color = message['color']
        if color[:1] != '#' or len(color) != 7:
            return 'a color that is no color: %r' % (color,)
        try:
            int(color[1:], 16)
        except ValueError:
            return 'a color that is no color: %r' % (color,)

    return None


threading.Thread(target=read_out, daemon=True).start()
time.sleep(SETTLE)
send({'type': 'resize', 'width': 1196, 'height': 800})
time.sleep(3)
emacs.kill()

if not lines:
    sys.exit('Emacs said nothing to draw')

kinds = {}
glyphs = 0
for message in lines:
    kinds[message.get('op')] = kinds.get(message.get('op'), 0) + 1
    if message.get('op') == 'glyphs':
        glyphs += len(message.get('ids', []))
    if why := check(message):
        wrong.append(why)

print('%d lines, all of them JSON' % len(lines))
for op, count in sorted(kinds.items(), key=lambda pair: -pair[1]):
    print('  %-10s %d' % (op, count))
print('  %d glyphs in all' % glyphs)

if wrong:
    print('\n%d that the host could not read:' % len(wrong))
    for why in wrong[:20]:
        print('  ' + why)
    sys.exit(1)

print('\nevery line is a kind of drawing the host reads, with the fields it'
      ' reads')


def written(message):
    """One command as the test reads it: the kind, then its fields."""
    op = message['op']

    if op in ('fill', 'rectangle', 'clip'):
        return '%s %d %d %d %d %06x' % (op, message['x'], message['y'],
                                        message['width'], message['height'],
                                        int(message.get('color', '#000000')[1:], 16))
    if op == 'line':
        return 'line %d %d %d %d %06x' % (message['x0'], message['y0'],
                                          message['x1'], message['y1'],
                                          int(message['color'][1:], 16))
    if op == 'copy':
        return 'copy %d %d %d %d %d' % (message['x'], message['y'],
                                        message['width'], message['height'],
                                        message['toY'])
    if op == 'unclip':
        return 'unclip'
    if op == 'glyphs':
        return 'glyphs %d %s %d %06x %d %s %s' % (
            message['font'], repr(float(message['size'])), message['y'],
            int(message['color'][1:], 16), len(message['ids']),
            ' '.join(str(number) for number in message['ids']),
            ' '.join(str(number) for number in message['xs']))
    return None


if RECORD:
    # The first whole screen: one that began and ended, with the most
    # in it, which is the one drawn when the frame was sized.
    screens = []
    holding = None
    for message in lines:
        if message['op'] == 'begin':
            holding = {'width': message['width'], 'height': message['height'],
                       'lines': [], 'commands': []}
        elif holding is None:
            continue
        elif message['op'] == 'end':
            screens.append(holding)
            holding = None
        else:
            holding['lines'].append(message)
            holding['commands'].append(written(message))

    if not screens:
        sys.exit('no whole screen to write down')

    fullest = max(screens, key=lambda screen: len(screen['commands']))
    where = RECORD.rstrip('/\\')

    with io.open(where + '/draw-screen.jsonl', 'w', encoding='utf-8',
                 newline='\n') as file:
        file.write(json.dumps({'type': 'draw', 'op': 'begin', 'frame': 'f',
                               'width': fullest['width'],
                               'height': fullest['height']},
                              separators=(',', ':')) + '\n')
        for message in fullest['lines']:
            file.write(json.dumps(message, separators=(',', ':')) + '\n')
        file.write(json.dumps({'type': 'draw', 'op': 'end', 'frame': 'f'},
                              separators=(',', ':')) + '\n')

    with io.open(where + '/draw-screen.expected', 'w', encoding='utf-8',
                 newline='\n') as file:
        file.write('screen %d %d %d\n' % (fullest['width'], fullest['height'],
                                          len(fullest['commands'])))
        for command in fullest['commands']:
            file.write(command + '\n')

    print('wrote a screen of %d commands to %s'
          % (len(fullest['commands']), where))
