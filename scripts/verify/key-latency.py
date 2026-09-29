"""How long a key of the host's waits before the screen shows it.

  python scripts/verify/key-latency.py

A key goes to a queue of C's, not to Lisp's, and Emacs reads it as it
reads any input; what is measured is from sending the key to the screen
that carries the character it typed.  Run it against either build (the
host one is taken where there is one, as the tests take it).
"""
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'tests', 'host'))

from emacs_host import EmacsHost  # noqa: E402


def main():
    # How often Lisp looks for messages, for telling what the wakeup
    # does from what the looking does: a key that waits the interval is
    # a key nothing woke Emacs for.
    interval = None
    if len(sys.argv) > 2 and sys.argv[1] == '--interval':
        interval = sys.argv[2]

    # A key of its own that says so as soon as it is read, to tell the
    # reading of a key from the drawing that follows it.
    said = ('(global-set-key [f9]'
            ' (lambda () (interactive) (urushi--send (list :type "pong"))))')
    arguments = ['--eval', said]
    if interval:
        arguments += ['--eval', '(setq urushi-poll-interval %s)' % interval]

    emacs = EmacsHost(*arguments)
    try:
        emacs.wait_for('hello')
        emacs.wait_for('picture')

        # What the key ran, which is the key alone: a message back from
        # the command it is bound to, before anything is drawn.
        ran = []
        for _ in range(10):
            began = time.monotonic()
            emacs.send({'type': 'key', 'name': 'f9'})
            emacs.wait_for('pong')
            ran.append((time.monotonic() - began) * 1000)

        waits = []
        for n in range(10):
            began = time.monotonic()
            emacs.send({'type': 'key', 'char': ord('abcdefghij'[n])})
            # What the key changed, drawn: the frame is a picture here,
            # as it is for a host that does not draw what Emacs says.
            emacs.wait_for('picture')
            waits.append((time.monotonic() - began) * 1000)

        print('key to what it ran:  %d rounds, %.1f ms on average, %.1f at worst'
              % (len(ran), statistics.fmean(ran), max(ran)))
        print('key to what it drew: %d rounds, %.1f ms on average, %.1f at worst'
              % (len(waits), statistics.fmean(waits), max(waits)))
    finally:
        emacs.close()


if __name__ == '__main__':
    main()
