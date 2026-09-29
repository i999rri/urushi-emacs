"""The fonts Emacs picks, as they reach the host.

  python -m unittest discover -s tests/host

Emacs on the `host' window system reads font files itself (sfnt) and
names, in the XAML of each row, the family the host is to draw it in.
These tests type into an Emacs of their own and read that name back.

They need the fonts they name to be installed, and are skipped where
they are not; and, like the input tests, an Emacs on the `host' window
system.
"""

import re
import unittest

from test_input import HostInput

JAPANESE = 'さ'

# Sarasa Gothic, whose first name record is Chinese: the family Emacs
# reports says whether the English record was preferred.
FAMILY = 'Sarasa Mono J'

SET_UP_FONTS = r'''
(progn
  (urushi--log "font-found %S" (and (find-font (font-spec :family "Sarasa Mono J")) t))
  (urushi--log "font-families %S"
              (seq-filter (lambda (family)
                            (string-match-p "Sarasa\\|更紗\\|更纱" family))
                          (font-family-list)))
  (urushi--log "default-font %S"
              (let ((info (font-info (frame-parameter nil 'font))))
                (list (aref info 0) (aref info 12))))
  (dolist (script '(kana han cjk-misc))
    (set-fontset-font t script (font-spec :family "Sarasa Mono J"))))
'''

# A run of text and the family it is drawn in, as urushi-screen writes them.
RUN = re.compile(r'<TextBlock Text="([^"]*)"[^>]*FontFamily="([^"]*)"')


class FontTest(HostInput, unittest.TestCase):

    ARGS = ('--eval', SET_UP_FONTS)

    def font_log(self, prefix):
        """What Emacs logged for PREFIX, without the prefix.

        These are logged as Emacs starts, before setUp has its first
        screen, so they are looked for among the messages already seen
        rather than waited for."""
        for text in self.emacs.logs():
            if text.startswith(prefix):
                return text[len(prefix):]
        self.fail(f'Emacs logged nothing for {prefix!r}')

    def need_font(self):
        if self.font_log('font-found ') != 't':
            self.skipTest(f'{FAMILY} is not installed')

    def family_of(self, text):
        """The family of the run TEXT is drawn in, once it is on the screen."""
        self.emacs.send({'type': 'text', 'text': text})
        self.wait_for_lines(lambda lines: any(text in line for line in lines))
        for xaml in self.screen.xaml.values():
            for run, family in RUN.findall(xaml):
                if text in run:
                    return family
        self.fail(f'{text} is on the screen but in no run')

    def test_japanese_is_drawn_in_the_font_the_fontset_names(self):
        """A character the fontset gives a family is sent in that family, not
        in the family of the default font."""
        self.need_font()

        self.assertEqual(FAMILY, self.family_of(JAPANESE))

    def test_a_family_is_named_in_english(self):
        """A font whose first name record is not English is still named in
        English, which is the name a host has for it."""
        self.need_font()
        families = self.font_log('font-families ')

        self.assertIn(f'"{FAMILY}"', families)
        self.assertTrue(families.isascii(), families)

    def test_the_default_font_comes_from_a_font_file(self):
        """A frame with no font of its own gets one of the monospaced fonts
        the font files found have, rather than failing to be made."""
        name, filename = re.findall(r'"([^"]*)"', self.font_log('default-font '))

        self.assertTrue(name.startswith('-'), name)
        self.assertTrue(filename.endswith(('.ttf', '.ttc', '.otf', '.otc')), filename)


if __name__ == '__main__':
    unittest.main()
