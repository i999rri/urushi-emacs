"""Child frames on the `host' window system: frames that float over
another, as a minibuffer or a completion popup does.

  python -m unittest discover -s tests/host

The frame is made from Lisp once Emacs has started, and what Emacs
makes of it is logged for the test to read.  Like the input tests,
these need an Emacs on the `host' window system, and are skipped on
one that is not.
"""

import unittest

from test_input import HostInput

# Makes a child frame of the selected frame at 30, 40, 20 columns by 5
# lines, and logs what it is.
MAKE_CHILD = r'''
(run-at-time 1 nil
  (lambda ()
    (let* ((root (selected-frame))
           (child (make-frame `((parent-frame . ,root) (left . 30) (top . 40)
                                (width . 20) (height . 5) (minibuffer . nil)))))
      (urushi--log "child parent %s position %S size %S root-size %S"
                  (eq (frame-parent child) root)
                  (frame-position child)
                  (cons (frame-pixel-width child) (frame-pixel-height child))
                  (cons (frame-pixel-width root) (frame-pixel-height root)))
      (set-frame-position child 50 60)
      (urushi--log "moved position %S" (frame-position child)))))
'''


class ChildFrameTest(HostInput, unittest.TestCase):

    ARGS = ('--eval', MAKE_CHILD)

    def child_log(self, prefix):
        message = self.emacs.wait_for('log', lambda m: m.get('text', '').startswith(prefix))
        return message['text']

    def test_a_child_frame_is_a_child_where_it_was_put(self):
        """A frame made with a parent has it, at the place it was given, and
        is its own size rather than the size of the window."""
        text = self.child_log('child ')

        self.assertIn('parent t', text)
        self.assertIn('position (30 . 40)', text)
        size, root_size = text.split(' size ')[1].split(' root-size ')
        width = int(size.strip('()').split(' . ')[0])
        root_width = int(root_size.strip('()').split(' . ')[0])
        self.assertLess(width, root_width)

    def test_a_child_frame_moves(self):
        """set-frame-position moves a child frame within its parent."""
        self.assertIn('position (50 . 60)', self.child_log('moved '))


if __name__ == '__main__':
    unittest.main()
