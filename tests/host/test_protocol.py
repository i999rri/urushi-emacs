"""The protocol, as Emacs speaks it to a host.

  python -m unittest discover -s tests/host

Each test starts an Emacs of its own, with urusi and none of the
user's init, and talks to it as an application would (emacs_host.py).
On Windows the frames are still Emacs's own windows, and one shows for
as long as each test runs.
"""

import unittest

from emacs_host import EmacsHost, rows_of


class ProtocolTest(unittest.TestCase):

    def setUp(self):
        self.emacs = EmacsHost()
        self.addCleanup(self.emacs.close)

    def test_hello_is_answered_and_a_screen_follows(self):
        """Emacs says hello first, and once answered, shows its screen."""
        hello = self.emacs.wait_for('hello')
        self.assertEqual(hello.get('version'), 1)
        # Which window system draws the frames, which is how the host
        # knows whether they are windows it can post input to.
        self.assertEqual(hello.get('window-system'), 'host')

        screen = self.emacs.wait_for('screen', lambda m: 'xaml' in m)
        self.assertIn('urusi-frame', screen['xaml'])
        self.assertTrue(rows_of(screen, 'window-0'), 'the first window has rows')

    def test_a_smaller_frame_has_fewer_rows(self):
        """resize is the room the frame has, and Emacs lays out to it."""
        self.emacs.wait_for('screen', lambda m: 'xaml' in m)

        self.emacs.send({'type': 'resize', 'width': 640, 'height': 480})
        tall = self.emacs.wait_for('screen', lambda m: rows_of(m, 'window-0'))
        self.emacs.send({'type': 'resize', 'width': 640, 'height': 160})
        short = self.emacs.wait_for(
            'screen', lambda m: 0 < len(rows_of(m, 'window-0')) < len(rows_of(tall, 'window-0')))

        self.assertLess(len(rows_of(short, 'window-0')), len(rows_of(tall, 'window-0')))

    def test_stale_has_the_whole_screen_sent_again(self):
        """A host that lost track of the screen gets all of it again."""
        self.emacs.wait_for('screen', lambda m: 'xaml' in m)

        self.emacs.send({'type': 'stale'})
        again = self.emacs.wait_for('screen', lambda m: 'xaml' in m)

        self.assertTrue(all('xaml' in item for item in rows_of(again, 'window-0')),
                        'every row comes with its XAML')

    def test_lisp_calls_the_host(self):
        """Lisp asks the host things by name, with an id to answer to."""
        call = self.emacs.wait_for('call', lambda m: m.get('method') == 'window.title')

        self.assertIsInstance(call.get('id'), int)
        self.assertIn('title', call.get('args', {}))

    def test_an_error_leaves_emacs_talking(self):
        """Being told the host could not do something stops nothing."""
        self.emacs.wait_for('screen', lambda m: 'xaml' in m)

        self.emacs.send({'type': 'error', 'message': 'no panel named nowhere'})
        self.emacs.send({'type': 'stale'})
        self.emacs.wait_for('screen', lambda m: 'xaml' in m)


if __name__ == '__main__':
    unittest.main()
