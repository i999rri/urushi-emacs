"""Input, as the host sends it: keys, text, the pointer and the focus.

  python -m unittest discover -s tests/host

Each test types into an Emacs of its own and reads what it typed back
from the screen Emacs sends.  They need an Emacs on the `host' window
system, which reads the host's input messages itself; the Emacs of
Windows still takes its keys and pointer as Windows messages to its
frame windows, and has nothing to read these with, so there they are
skipped.  Which it is, Emacs says as it starts (see setUp).
"""

import html
import re
import unittest

from emacs_host import EmacsHost, rows_of

TEXT = re.compile(r'<TextBlock Text="([^"]*)"')


class Screen:
    """The rows of the screen as the host would have them.

    A row the host already has comes with only its key, so the rows
    are kept by key from one screen to the next, as the host keeps
    them."""

    def __init__(self):
        self.xaml = {}

    def take(self, message):
        """Take the rows of MESSAGE, a screen."""
        for group in message.get('rows', []):
            for item in group.get('items', []):
                if 'xaml' in item:
                    self.xaml[(group.get('panel'), item['key'])] = item['xaml']
        return message

    def lines(self, message, panel='window-0'):
        """The text of each row of PANEL in MESSAGE, top to bottom.

        Without the spaces at the end, which are where the cursor or the
        end of the line is drawn, and not text."""
        self.take(message)
        return [''.join(html.unescape(text)
                        for text in TEXT.findall(self.xaml.get((panel, item['key']), '')))
                .rstrip()
                for item in rows_of(message, panel)]


class HostInput:
    """Starting Emacs to type into, and typing."""

    # More of Emacs's command line, after urushi is loaded.
    ARGS = ()

    def setUp(self):
        self.emacs = EmacsHost('--eval', '(urushi--log "window-system %s" window-system)',
                               *self.ARGS)
        self.addCleanup(self.emacs.close)
        self.screen = Screen()
        self.screen.take(self.emacs.wait_for('screen', lambda m: 'xaml' in m))
        if 'window-system host' not in self.emacs.logs():
            self.skipTest('input reaches this Emacs as Windows messages, not as messages')
        self.emacs.send({'type': 'resize', 'width': 640, 'height': 480})
        self.first_lines = self.screen.lines(
            self.emacs.wait_for('screen', lambda m: rows_of(m, 'window-0')))

    # ----- Saying things -----

    def key(self, char=None, name=None, modifiers=()):
        """Press and let go of a key, CHAR or the one called NAME."""
        key = {'char': char} if char is not None else {'name': name}
        for down in (True, False):
            self.emacs.send({'type': 'key', 'down': down, 'modifiers': list(modifiers),
                             'repeat': False, **key})

    def type(self, text):
        """Type TEXT, one key to a character."""
        for char in text:
            self.key(char)

    def pointer(self, kind, x, y, **more):
        self.emacs.send({'type': 'pointer', 'kind': kind, 'x': x, 'y': y,
                         'modifiers': [], **more})

    # ----- Looking -----

    def wait_for_lines(self, test, timeout=20):
        """The lines of the first screen for which TEST of its lines is true."""
        found = []

        def check(message):
            lines = self.screen.lines(message)
            if lines and test(lines):
                found.append(lines)
                return True
            return False

        self.emacs.wait_for('screen', check, timeout=timeout)
        return found[0]

    def wait_for_text(self, text):
        """The lines of the first screen with a line that has TEXT in it."""
        return self.wait_for_lines(lambda lines: any(text in line for line in lines))


class InputTest(HostInput, unittest.TestCase):

    def test_typed_characters_are_inserted(self):
        """A key with a character types it."""
        self.type('hello')
        self.wait_for_text('hello')

    def test_return_starts_a_new_line(self):
        """return is a named key, and makes a new line."""
        self.type('abc')
        self.key(name='return')
        self.type('def')
        lines = self.wait_for_text('def')

        self.assertTrue(any(line.endswith('abc') for line in lines), lines)
        self.assertFalse(any('abcdef' in line for line in lines), lines)

    def test_backspace_deletes_backward_and_delete_forward(self):
        """backspace deletes the character before the cursor, delete the one after."""
        self.type('wxyz')
        self.key(name='backspace')
        self.key(name='left')
        self.key(name='left')
        self.key(name='delete')
        self.type('!')
        self.wait_for_text('w!y')

    def test_control_a_goes_to_the_start_of_the_line(self):
        """ctrl with a character is that character's control key: C-a."""
        self.type('abc')
        self.key('a', modifiers=['ctrl'])
        self.type('X')
        self.wait_for_text('Xabc')

    def test_text_from_the_input_method_is_inserted(self):
        """text is what the input method settled on, typed as its characters."""
        self.type('<')
        self.emacs.send({'type': 'text', 'text': 'über'})
        self.type('>')
        self.wait_for_text('<über>')

    def test_a_click_moves_point(self):
        """A press and a release of the first button put point where they were."""
        self.type('abcdef')
        self.wait_for_text('abcdef')
        # Where the cursor was drawn, which is sent before the screen.
        caret = [m for m in self.emacs.seen if m.get('type') == 'caret'][-1]
        cell = caret['width']

        # Inside the d, three characters back from the end.
        x, y = caret['x'] - 3 * cell + 2, caret['y'] + caret['height'] // 2
        self.pointer('move', x, y)
        self.pointer('down', x, y, button=1, clicks=1)
        self.pointer('up', x, y, button=1)
        self.type('Z')
        self.wait_for_text('abcZdef')

    def test_losing_and_getting_the_focus_leaves_keys_working(self):
        """focus goes out and comes back, and keys still type."""
        self.emacs.send({'type': 'focus', 'focused': False})
        self.emacs.send({'type': 'focus', 'focused': True})
        self.type('still')
        self.wait_for_text('still')
        self.assertFalse([line for line in self.emacs.logs() if 'error' in line.lower()],
                         self.emacs.logs())


class WheelTest(HostInput, unittest.TestCase):
    """The wheel, over a buffer long enough to scroll, shown from its top."""

    ARGS = ('--eval', '(progn (erase-buffer) (dotimes (i 200) (insert (format "line %d\\n" i)))'
                      ' (goto-char (point-min)))')

    def test_the_wheel_scrolls(self):
        """The wheel toward the person shows what is further down."""
        self.assertEqual(self.first_lines[0], 'line 0')

        self.pointer('move', 100, 100)
        self.pointer('wheel', 100, 100, dx=0, dy=-3)
        lines = self.wait_for_lines(lambda lines: lines[0].startswith('line ')
                                    and lines[0] != 'line 0')

        self.assertRegex(lines[0], r'^line [1-9]')


if __name__ == '__main__':
    unittest.main()
