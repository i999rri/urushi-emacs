"""The host's messages reaching Lisp because Emacs was told of them.

  python -m unittest discover -s tests/host

The host writes to a pipe Emacs waits on, so Emacs learns of a message
as it is sent; `host-message-function' is what the command loop calls
for the messages that are not input.  Lisp can instead look for itself
on a timer, and does by default, which would answer every test here
whether the telling worked or not.  So each test stops the timer first:
with nothing looking, a message that arrives arrived because Emacs was
told.

They need an Emacs on the `host' window system, which reads the host's
messages itself.  The Emacs of Windows takes its input as Windows
messages to its frame windows and has none of this, so there they are
skipped; which it is, Emacs says as it starts.
"""

import time
import unittest

from emacs_host import EmacsHost

# One argument, so no newlines: what Emacs is asked to do once urusi has
# started.  The asking is hung on `urusi-start' rather than done here,
# because the command line is read before urusi starts and there would
# be no timer to stop yet.
#
# `host-message-function' is set here as well as wherever urusi sets it,
# so that what these tests are about does not depend on that.
SETUP = ' '.join((
    '(progn',
    '  (urusi--log "window-system %s" window-system)',
    "  (add-hook 'urusi-message-hook",
    '            (lambda (message)',
    '              (pcase (plist-get message :type)',
    '                ("probe" (urusi--log "probe %s" (plist-get message :n)))',
    '                ("say-buffer"',
    '                 (urusi--log "buffer %s"',
    '                             (buffer-substring-no-properties',
    '                              (point-min) (point-max)))))))',
    "  (advice-add 'urusi-start :after",
    '              (lambda (&rest _)',
    '                (urusi-stop)',
    "                (setq host-message-function #'urusi--take)",
    '                (urusi--log "told, not looking"))))',
))

# What the timer would have cost, for the measurement to be read against.
POLL_INTERVAL = 0.05


class Told(unittest.TestCase):
    """Emacs told of the host's messages, with nothing looking for them."""

    def setUp(self):
        self.emacs = EmacsHost('--eval', SETUP)
        self.addCleanup(self.emacs.close)
        self.emacs.wait_for('log', lambda m: m.get('text', '').startswith('window-system'))
        if 'window-system host' not in self.emacs.logs():
            self.skipTest('this Emacs reads its input as Windows messages, not as messages')
        self.emacs.wait_for('log', lambda m: m.get('text') == 'told, not looking')

    # ----- Saying things -----

    def probe(self, n):
        """Send the Nth message that is not input, and not for the host."""
        self.emacs.send({'type': 'probe', 'n': n})

    def type(self, text):
        """Type TEXT, which goes the other way, through the keyboard buffer."""
        for char in text:
            for down in (True, False):
                self.emacs.send({'type': 'key', 'down': down, 'char': char,
                                 'modifiers': [], 'repeat': False})

    # ----- Looking -----

    def wait_for_probe(self, n, timeout=20):
        """Wait until Lisp has handled the Nth message."""
        return self.emacs.wait_for('log', lambda m: m.get('text') == 'probe %d' % n,
                                   timeout=timeout)

    def buffer_text(self, timeout=20):
        """What is in the buffer Emacs is showing, asked for and waited for."""
        self.emacs.send({'type': 'say-buffer'})
        said = self.emacs.wait_for('log', lambda m: m.get('text', '').startswith('buffer '),
                                   timeout=timeout)
        return said['text'][len('buffer '):]

    # ----- The tests -----

    def test_a_message_arrives_with_nothing_looking_for_it(self):
        """The telling works at all: one message, no timer, and it lands."""
        self.probe(1)
        self.wait_for_probe(1)

    def test_each_message_arrives_when_it_is_the_only_one(self):
        """A message sent only once the one before it was handled arrives too.

        Every message is announced by one event in the keyboard buffer,
        and only one at a time, since a second would say nothing new and
        would crowd out the keys waiting there.  The flag that says one
        is outstanding has to go back down once Lisp has taken the
        queue, or the message after it is never announced and waits for
        something else to wake Emacs.  Sent one at a time, each after
        the last was handled, this is the only way it could arrive.
        """
        for n in range(1, 6):
            self.probe(n)
            self.wait_for_probe(n)

    def test_a_burst_of_messages_all_arrive(self):
        """Sent faster than they are handled, none is lost.

        Well under the 4096 a queue holds, past which the oldest are
        dropped to keep a queue from growing without bound."""
        many = 200
        for n in range(1, many + 1):
            self.probe(n)
        self.wait_for_probe(many, timeout=60)

        # Said down one pipe, in order, so the last one seen means the
        # ones before it were seen too.
        said = set(self.emacs.logs())
        missing = [n for n in range(1, many + 1) if 'probe %d' % n not in said]
        self.assertEqual(missing, [], 'lost %d of %d' % (len(missing), many))

    def test_keys_get_through_a_burst_of_messages(self):
        """Typing still arrives while messages are being announced.

        The announcements ride in the keyboard buffer, which is where
        keys and the pointer wait, and it is not endless: announcing
        every message rather than one at a time filled it and pushed the
        typing out, so that nothing typed or clicked arrived at all.
        """
        # Counted from what is there already: the buffer Emacs starts in
        # has text of its own, and some of it is the letter typed here.
        typed = 50
        before = self.buffer_text().count('x')
        for n in range(1, typed + 1):
            self.probe(n)
            self.type('x')
        self.wait_for_probe(typed, timeout=60)

        # The keys are in a queue of their own, so the last of them may
        # still be on its way when the last message has been handled.
        end = time.monotonic() + 20
        while True:
            text = self.buffer_text()
            if text.count('x') - before >= typed or time.monotonic() > end:
                break
        self.assertEqual(text.count('x') - before, typed)

    def test_being_told_is_quicker_than_looking_would_have_been(self):
        """Measured, since when a message arrives is the whole point.

        The number is the round trip: sent from here, handled in Lisp,
        and said back down the pipe.  A timer that looks every
        POLL_INTERVAL costs half that again on average, so a median
        under one interval is the least this has to beat.
        """
        waits = []
        for n in range(1, 21):
            start = time.monotonic()
            self.probe(n)
            self.wait_for_probe(n)
            waits.append(time.monotonic() - start)

        waits.sort()
        median = waits[len(waits) // 2]
        print('\n  told in %.2f ms (median of %d), looked for every %.0f ms'
              % (median * 1000, len(waits), POLL_INTERVAL * 1000))
        self.assertLess(median, POLL_INTERVAL)


if __name__ == '__main__':
    unittest.main()
