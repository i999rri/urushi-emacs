"""Images loading on a host frame.

  python -m unittest discover -s tests/host

An image is decoded by Emacs into pixels of its own and the host draws
what it decoded, so the first thing to hold is that Emacs can decode one
at all: a window system has to give image.c somewhere to put the pixels,
and until it does nothing loads, not even the formats image.c reads
without any library.

What is loaded is asked about as Emacs starts, in one Emacs for all of
these, because none of them changes anything: they read what the first
moment of a host frame already decided.
"""

import base64
import unittest

from emacs_host import EmacsHost

# A file of bytes, written rather than kept beside this: what is tested
# is the decoding, and two lines of PBM say as much as a file would.
PBM = '/tmp/urushi-test-image.pbm'

# An SVG given as text rather than as a file, because that is the path a
# file name cannot stand in for: Lisp makes the image up, librsvg draws
# it, and its size is one Emacs worked out rather than one a file stated.
SVG = ('<svg xmlns=\\"http://www.w3.org/2000/svg\\" width=\\"12\\" height=\\"7\\">'
       '<rect width=\\"12\\" height=\\"7\\" fill=\\"red\\"/></svg>')

PROBE = ' '.join((
    '(progn',
    '  (urushi--log "window-system %s" window-system)',
    '  (urushi--log "types %S"',
    "              (seq-filter #'image-type-available-p",
    "                          '(pbm xbm png jpeg gif tiff svg webp)))",
    '  (with-temp-file "%s" (insert "P1\\n4 2\\n1 0 1 0\\n0 1 0 1\\n"))' % PBM,
    '  (urushi--log "empty %S" (image-cache-size))',
    '  (urushi--log "pbm %S"',
    '              (condition-case error',
    '                  (let ((image (create-image "%s" (quote pbm) nil)))' % PBM,
    '                    (list :size (image-size image t)',
    '                          :cache (image-cache-size)))',
    '                (error error)))',
    '  (urushi--log "svg %S"',
    '              (condition-case error',
    '                  (let ((image (create-image "%s" (quote svg) t)))' % SVG,
    '                    (list :size (image-size image t)))',
    '                (error error)))',
    '  (urushi--log "asked"))',
))


class Images(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.emacs = EmacsHost('--eval', PROBE)
        cls.emacs.wait_for('log', lambda m: m.get('text') == 'asked')
        cls.said = {}
        for line in cls.emacs.logs():
            kind, _, rest = line.partition(' ')
            cls.said[kind] = rest

    def setUp(self):
        # The Emacs of Windows takes its input and draws its frames as
        # Windows does and has none of this, as in test_input.py.
        if self.said.get('window-system') != 'host':
            self.skipTest('this Emacs has no host frames to load images for')

    @classmethod
    def tearDownClass(cls):
        cls.emacs.close()

    def test_the_formats_read_without_a_library_are_there(self):
        """PBM and XBM come with image.c, so a port that loads anything
        loads these."""
        self.assertIn('pbm', self.said['types'])
        self.assertIn('xbm', self.said['types'])

    def test_a_file_of_pixels_loads_and_is_the_size_it_says(self):
        """The plainest path there is: decoded straight into the pixmap,
        one pixel at a time, with no library in between."""
        self.assertNotIn('error', self.said['pbm'])
        self.assertIn('(4 . 2)', self.said['pbm'])

    def test_what_is_loaded_is_counted_against_the_image_cache(self):
        """A pixmap nothing accounts for is one nothing ever drops, so
        the cache has to see it grow."""
        self.assertEqual(self.said['empty'], '0')
        self.assertNotEqual(self.said['pbm'].split(':cache ')[1].rstrip(')'), '0')

    def test_an_image_made_up_by_lisp_loads(self):
        """Text handed to librsvg, not a file read from disk: a host that
        was given file names to load for itself could not show this one,
        and Emacs's own image code can."""
        if 'svg' not in self.said['types']:
            self.skipTest('this Emacs was built without librsvg')
        self.assertNotIn('error', self.said['svg'])
        self.assertIn('(12 . 7)', self.said['svg'])


class Drawn(unittest.TestCase):
    """An image said rather than drawn, and its pixels asked for.

    The pixels go over once and are drawn as often as they are wanted, as
    the file of a font does: a screen that showed an image would
    otherwise carry it again every time anything on that row changed.
    """

    # The image is 4 by 2 and every pixel of it is white, so that what
    # comes back can be checked without decoding anything.
    SHOWN = '/tmp/urushi-test-shown.pbm'
    SETUP = ' '.join((
        '(progn',
        '  (urushi--log "window-system %s" window-system)',
        '  (with-temp-file "%s" (insert "P1\\n4 2\\n0 0 0 0\\n0 0 0 0\\n"))' % SHOWN,
        '  (switch-to-buffer (get-buffer-create "shown"))',
        '  (erase-buffer)',
        '  (insert-image (create-image "%s" (quote pbm) nil))' % SHOWN,
        '  (urushi--log "shown"))',
    ))

    def setUp(self):
        class Host(EmacsHost):
            DRAWS = True

        self.emacs = Host('--eval', self.SETUP)
        self.addCleanup(self.emacs.close)
        self.emacs.wait_for('log', lambda m: m.get('text') == 'shown')
        if 'window-system host' not in ' '.join(self.emacs.logs()):
            self.skipTest('this Emacs has no host frames to draw images on')
        self.emacs.send({'type': 'resize', 'width': 640, 'height': 480})

    def test_an_image_is_said_with_a_number_and_not_its_pixels(self):
        """What a row says of an image is where it goes and which one it
        is; the pixels are nowhere in it."""
        said = self.emacs.wait_for('draw', lambda m: m.get('op') == 'image')

        self.assertIn('image', said)
        self.assertEqual(said['width'], 4)
        self.assertEqual(said['height'], 2)
        self.assertNotIn('pixels', said)
        # How big it is and where it goes, and nothing of what it is.
        self.assertEqual(said['imageWidth'], 4)
        self.assertEqual(said['imageHeight'], 2)
        self.assertEqual(len(said['matrix']), 6)

    def test_the_pixels_come_when_they_are_asked_for(self):
        """The host asks by the number and Emacs answers with the pixels,
        four bytes to a pixel, as they are to be drawn."""
        said = self.emacs.wait_for('draw', lambda m: m.get('op') == 'image')
        self.emacs.send({'type': 'want-image', 'id': said['image']})

        sent = self.emacs.wait_for('image', lambda m: m.get('id') == said['image'])
        self.assertEqual(sent['width'], 4)
        self.assertEqual(sent['height'], 2)

        pixels = base64.b64decode(sent['pixels'])
        self.assertEqual(len(pixels), 4 * 2 * 4)
        # White, and shown: the PBM says 0 for white, and with no mask
        # every pixel of it is drawn.
        self.assertEqual(pixels[:4], bytes((0xff, 0xff, 0xff, 0xff)))


if __name__ == '__main__':
    unittest.main()
