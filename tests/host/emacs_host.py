"""A host for tests: Emacs on a pipe, and nothing else.

The application loads Emacs and draws what Emacs lays out.  This does
neither: it starts Emacs with the host's table on its standard input
and output (EMACS_HOST_PIPE), and says to it what the application
would, so that what Emacs says back can be looked at in a test, on any
system, with no window.

What an application answers without being asked, it answers here too,
the same way each time: hello, the width of a font, and calls.
"""

import json
import os
import queue
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
LISP = os.path.join(REPO, 'lisp')


def default_emacs():
    """The Emacs built in the tree, unless URUSI_TEST_EMACS says another."""
    if os.environ.get('URUSI_TEST_EMACS'):
        return os.environ['URUSI_TEST_EMACS']
    name = 'emacs.exe' if sys.platform == 'win32' else 'emacs'
    return os.path.join(REPO, 'external', 'emacs-build', 'src', name)


class EmacsHost:
    """One Emacs, started with urusi and nothing of the user's.

    ARGS go on its command line after urusi is loaded, --eval for one,
    to set up what a test looks at."""

    # What each answer says of the host.
    SCALE = 1.0
    NARROW = 8.0
    WIDE = 16.0

    def __init__(self, *args, emacs=None):
        self.messages = queue.Queue()
        self.seen = []
        self._lock = threading.Lock()
        env = dict(os.environ, EMACS_HOST_PIPE='1')
        command = [emacs or default_emacs(), '-Q', '-L', LISP,
                   '-l', os.path.join(LISP, 'urusi-site-start.el'), *args]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, env=env)
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    # ----- Talking -----

    def send(self, message):
        """Send MESSAGE, a dict, to Emacs."""
        line = json.dumps(message, ensure_ascii=False) + '\n'
        with self._lock:
            self.process.stdin.write(line.encode('utf-8'))
            self.process.stdin.flush()

    # How long Emacs may take to say anything at all.  The first start
    # of an Emacs just built has taken over 20 s on Windows, where the
    # starts after it take under one.
    STARTUP_TIMEOUT = 180

    def wait_for(self, kind, test=None, timeout=20):
        """The next message of type KIND for which TEST, if given, is true.

        Messages of other types, or that TEST turns down, are passed
        over; all of them are kept in self.seen either way.  TIMEOUT
        counts from Emacs's first message, or from now once there has
        been one."""
        if not self.seen:
            end = time.monotonic() + self.STARTUP_TIMEOUT
            while not self.seen and time.monotonic() < end:
                time.sleep(0.05)
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                kinds = [m.get('type') for m in self.seen[-20:]]
                raise AssertionError(f'no {kind} in {timeout}s; last seen: {kinds}')
            try:
                message = self.messages.get(timeout=left)
            except queue.Empty:
                continue
            if message.get('type') == kind and (test is None or test(message)):
                return message

    def logs(self):
        """The text of every log line Emacs has sent so far."""
        return [m.get('text', '') for m in self.seen if m.get('type') == 'log']

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=10)
        self.process.stdin.close()
        self.process.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ----- Answering -----

    def _read(self):
        for raw in self.process.stdout:
            line = raw.decode('utf-8', 'replace').rstrip('\r\n')
            try:
                message = json.loads(line)
            except ValueError:
                message = {'type': 'not-json', 'line': line}
            self.seen.append(message)
            self._answer(message)
            self.messages.put(message)

    def _answer(self, message):
        kind = message.get('type')
        if kind == 'hello':
            self.send({'type': 'hello', 'host': 'tests', 'version': 1,
                       'scale': self.SCALE, 'debug': False})
        elif kind == 'measure':
            self.send({'type': 'measured', 'family': message.get('family'),
                       'size': message.get('size'),
                       'narrow': self.NARROW, 'wide': self.WIDE})
        elif kind == 'call':
            self.send({'type': 'reply', 'id': message.get('id'), 'value': None})


def rows_of(screen, panel):
    """The row items of PANEL in SCREEN, or [] if it has none."""
    for group in screen.get('rows', []):
        if group.get('panel') == panel:
            return group.get('items', [])
    return []
