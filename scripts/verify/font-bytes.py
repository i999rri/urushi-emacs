"""Whether Emacs answers `want-font' with the file, and how long it takes.

  URUSHI_TEST_EMACS=~/dev/urushi/emacs-build/src/emacs python3 scripts/verify/font-bytes.py

The host draws a glyph by the file it is in, so it must have the file.
Where Emacs is loaded into the application, `font' says the path and the
application opens it: nothing crosses.  Where Emacs is a process of its
own -- the Emacs for Linux in WSL -- the file is on the other side of
the pipe, and the application asks for it with `want-font' and is sent
its bytes.

That round trip therefore runs on the remote path and nowhere else, and
nothing in tests/host covers it.  This asks for every font Emacs names
and says what came back.
"""

import base64
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), '..', 'tests', 'host'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tests', 'host'))

from emacs_host import EmacsHost  # noqa: E402


class Drawing(EmacsHost):
    """An application that draws what Emacs says, which is what makes
    Emacs say which file a font is."""

    DRAWS = True


def main():
    # A family to ask for, so that the font the person actually reads --
    # a Nerd Font, a CJK collection -- is measured and not whatever the
    # bare Emacs settled on.
    family = sys.argv[1] if len(sys.argv) > 1 else None
    args = ('--eval', '(set-frame-font "%s" nil t)' % family) if family else ()

    with Drawing(*args) as emacs:
        named = emacs.wait_for(
            'font', lambda m: m.get('file'), timeout=30)
        print('named  id {id}, {file}, {size} bytes'.format(
            id=named.get('id'), file=named.get('file'), size=named.get('size')))

        began = time.monotonic()
        emacs.send({'type': 'want-font', 'id': named['id']})

        try:
            sent = emacs.wait_for(
                'font', lambda m: m.get('id') == named['id'] and m.get('bytes'),
                timeout=30)
        except AssertionError as failed:
            print('NOTHING CAME BACK:', failed)
            return 1

        took = time.monotonic() - began
        raw = base64.b64decode(sent['bytes'])
        print('sent   {count} bytes in {took:.2f}s'.format(count=len(raw), took=took))

        if named.get('size') is not None and len(raw) != named['size']:
            print('MISMATCH: it said {said} bytes and sent {got}'.format(
                said=named['size'], got=len(raw)))
            return 1

        head = raw[:4]
        print('begins {head!r}, which is {what}'.format(
            head=head,
            what='a font file' if head in (b'\x00\x01\x00\x00', b'OTTO', b'ttcf', b'true')
            else 'NOT a font file'))
        return 0


if __name__ == '__main__':
    sys.exit(main())
