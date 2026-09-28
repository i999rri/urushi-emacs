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
    """The Emacs built in the tree, unless URUSI_TEST_EMACS says another.

    The host window system's build first: this is its protocol, and on
    Windows the other build there draws its own frames.  Where there is
    one build it is that one.
    """
    if os.environ.get('URUSI_TEST_EMACS'):
        return os.environ['URUSI_TEST_EMACS']
    name = 'emacs.exe' if sys.platform == 'win32' else 'emacs'
    builds = ('emacs-host-build', 'emacs-build')
    paths = [os.path.join(REPO, 'external', build, 'src', name)
             for build in builds]
    return next((path for path in paths if os.path.exists(path)), paths[-1])


def emacs_path():
    """PATH for Emacs to start with.

    On Windows, the Emacs of the build tree needs the DLLs of MSYS2's
    mingw64 (GnuTLS and the rest), which a shell of MSYS2 has on PATH and
    any other does not: started from PowerShell it does not start at
    all.  The places are the ones scripts/refresh-emacs.cmd looks in."""
    path = os.environ.get('PATH', '')
    if sys.platform != 'win32':
        return path
    for msys in (os.path.expandvars(r'%USERPROFILE%\scoop\apps\msys2\current'), r'C:\msys64'):
        bin_dir = os.path.join(msys, 'mingw64', 'bin')
        if os.path.isdir(bin_dir):
            return bin_dir + os.pathsep + path
    return path


class EmacsHost:
    """One Emacs, started with urusi and nothing of the user's.

    ARGS go on its command line after urusi is loaded, --eval for one,
    to set up what a test looks at."""

    # What each answer says of the host.
    SCALE = 1.0
    NARROW = 8.0
    WIDE = 16.0
    # Whether this host draws what Emacs says to draw rather than being
    # handed the pixels.  A test that looks at what Emacs says it drew
    # sets it; the rest are handed pictures, as before.
    DRAWS = False

    def __init__(self, *args, emacs=None):
        self.messages = queue.Queue()
        self.seen = []
        self._lock = threading.Lock()
        env = dict(os.environ, EMACS_HOST_PIPE='1', PATH=emacs_path())
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

    # How long Emacs may take to say anything at all, more than a message
    # takes once it is talking: it reads urusi's Lisp first.
    STARTUP_TIMEOUT = 60

    def wait_for(self, kind, test=None, timeout=20):
        """The next message of type KIND for which TEST, if given, is true.

        Messages of other types, or that TEST turns down, are passed
        over; all of them are kept in self.seen either way.  TIMEOUT
        counts from Emacs's first message, or from now once there has
        been one."""
        if not self.seen:
            end = time.monotonic() + self.STARTUP_TIMEOUT
            while not self.seen and time.monotonic() < end:
                self._check_running()
                time.sleep(0.05)
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if self.messages.empty():
                self._check_running()
            if left <= 0:
                kinds = [m.get('type') for m in self.seen[-20:]]
                raise AssertionError(f'no {kind} in {timeout}s; last seen: {kinds}')
            try:
                message = self.messages.get(timeout=left)
            except queue.Empty:
                continue
            if message.get('type') == kind and (test is None or test(message)):
                return message

    def _check_running(self):
        """Fail at once if Emacs has exited: nothing more is coming.
        The code says why, as 0xC0000135 says a DLL was not found."""
        code = self.process.poll()
        if code is not None and self.messages.empty():
            raise AssertionError(f'Emacs exited with {code} (0x{code & 0xFFFFFFFF:08X}) '
                                 f'after {len(self.seen)} messages')

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
                       'scale': self.SCALE, 'draws': self.DRAWS,
                       'debug': False})
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
