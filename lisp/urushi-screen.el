;;; urushi-screen.el --- The Emacs screen, built as XAML  -*- lexical-binding: t; -*-

;;; Commentary:

;; Sends what Emacs shows to the host, as a tree of XAML elements.
;;
;; What is on the screen is not worked out here.  Redisplay worked it
;; out -- where the lines were broken, what an overlay or a display
;; property put there instead of the text, which font each character was
;; found in, where the cursor ended up -- and `window-screen-rows' reads
;; that decision back.  This file says only what it should look like, and
;; that part is yours to change.
;;
;; The screen is a list of components, each a function of the frame
;; returning a tree for `urushi-render':
;;
;;   `urushi-screen-windows'  every window of the frame, where it is
;;
;; Take one out of `urushi-screen-components' and it is gone.  Write your
;; own and it is there.  Wrap one and it keeps its contents:
;;
;;   (defun my-rounded-windows (frame)
;;     `(Border :CornerRadius 8 :Padding 12 :Background "#1b1b1f"
;;              ,(urushi-screen-windows frame)))
;;   (setq urushi-screen-components '(my-rounded-windows))
;;
;; Inside that, each piece is built by a function of its own, so that
;; changing one changes that much and no more:
;; `urushi-screen-window-function' for a window, `-line-function' for one
;; of its lines, `-run-function' for a stretch of a line drawn one way,
;; `-cursor-function' for the cursor, and `-child-frame-function' for a
;; child frame, such as a minibuffer floating over the frame.

;;; Code:

(require 'cl-lib)
(require 'subr-x)
(require 'urushi)

(defgroup urushi-screen nil
  "The Emacs screen, built as XAML."
  :group 'urushi)

(defcustom urushi-screen-font "Consolas"
  "Font to draw in when Emacs names none for a run.
That is so on a terminal, where Emacs never asked for one."
  :type 'string)

;;;; Faces and colours

(defun urushi-screen--set (value)
  "Return VALUE, or nil if what it came from said nothing about it."
  (unless (eq value 'unspecified) value))

(defvar urushi-screen--colors (make-hash-table :test #'equal)
  "What each colour Emacs named comes to in XAML.
Looking a colour up is a search through a table of names, and a screen
asks after the same handful of them hundreds of times.")

(defun urushi-screen-color (color)
  "Return COLOR as the #rrggbb XAML wants, or nil if there is no such color."
  (if (not (stringp (urushi-screen--set color)))
      nil
    (let ((known (gethash color urushi-screen--colors 'unknown)))
      (if (not (eq known 'unknown))
          known
        (puthash color
                 (when-let* ((values (color-values color)))
                   (apply #'format "#%02x%02x%02x"
                          (mapcar (lambda (v) (/ v 256)) values)))
                 urushi-screen--colors)))))

(defun urushi-screen-font-family ()
  "Return the family of the default font, as a name XAML knows."
  (let ((family (urushi-screen--set (face-attribute 'default :family))))
    (if (and (stringp family) (not (equal family "default")))
        family
      urushi-screen-font)))

(defun urushi-screen-font-size ()
  "Return the size of the default font, in the pixels XAML counts in."
  (let ((height (urushi-screen--set (face-attribute 'default :height))))
    ;; A height is tenths of a point on a frame that has a font, and a
    ;; multiplier on one that has none.  XAML counts 96ths of an inch.
    (if (and (integerp height) (<= 10 height))
        (/ (* height 96.0) 720.0)
      14.0)))

;;;; What the host measured

(defvar urushi-screen--advance (make-hash-table :test #'equal)
  "How wide the host draws a character, as a table of fonts.
The key is (FAMILY . SIZE) and the value (NARROW . WIDE).

Emacs lays its text out on a grid of whole columns -- one for most
characters, two for the likes of kana -- while the host draws at the
width the font asks for, so the two drift apart across a line.  How
wide the host draws one of each is what tells Emacs how much to correct
for; see `urushi-screen--spacing'.")

(defvar urushi-screen--asked nil
  "Fonts the host is measuring and has not answered for yet.")

(defun urushi-screen--font (family size)
  "Return what names FAMILY at SIZE in `urushi-screen--advance'.
The size is rounded because it is asked for and answered across JSON,
and a hundredth of a pixel apart is the same font."
  (cons family (round (* 100 size))))

(defun urushi-screen--measure (family size)
  "Ask the host how wide it draws FAMILY at SIZE, if it has not said."
  (let ((font (urushi-screen--font family size)))
    (unless (or (gethash font urushi-screen--advance)
                (member font urushi-screen--asked))
      (push font urushi-screen--asked)
      (urushi--send (list :type "measure" :family family :size size)))))

(defun urushi-screen--measured (message)
  "Take what the host measured in MESSAGE, and draw again knowing it."
  (let ((font (urushi-screen--font (plist-get message :family)
                                  (plist-get message :size))))
    (setq urushi-screen--asked (delete font urushi-screen--asked))
    (puthash font
             (cons (plist-get message :narrow) (plist-get message :wide))
             urushi-screen--advance)
    (urushi--log "font %s at %s drawn %s/%s, cell %s"
                (car font) (cdr font)
                (plist-get message :narrow) (plist-get message :wide)
                (default-font-width))
    (urushi-forget)
    (urushi-screen-render)))

(defun urushi-screen--spacing (family size advance)
  "Return what to add to each character of a run to space it as Emacs did.
FAMILY and SIZE are the font it is drawn in and ADVANCE how far apart
Emacs put its characters, both in the pixels XAML counts in.  XAML
counts the answer in thousandths of the font size."
  (let* ((measured (gethash (urushi-screen--font family size)
                            urushi-screen--advance))
         (cell (/ (default-font-width) (float urushi-scale)))
         ;; Which of the two the run is drawn with is decided by how
         ;; much room Emacs made for it, as Emacs decided it there too.
         (drawn (and measured
                     (if (< advance (* 1.5 cell)) (car measured) (cdr measured)))))
    (if (and (numberp drawn) (< 0 drawn) (< 0 size))
        (round (* 1000 (/ (- advance drawn) size)))
      0)))

;;;; The pieces

(defcustom urushi-screen-run-function #'urushi-screen-run
  "Function that draws one stretch of a line.
It takes the run, as `window-screen-rows' gives it, how tall the line is
in the pixels XAML counts in, and how much of that is space between
lines and how much of the space is above the text, in the same pixels.
See `urushi-screen-run'."
  :type 'function)

(defcustom urushi-screen-line-function #'urushi-screen-line
  "Function that draws one line of a window.
It takes the line, as `window-screen-rows' gives it."
  :type 'function)

(defcustom urushi-screen-cursor-function #'urushi-screen-cursor
  "Function that draws the cursor of a window, or nothing.
It takes the window."
  :type 'function)

(defcustom urushi-screen-composing-function #'urushi-screen-composing
  "Function that builds what the input method is composing.
It takes the frame and returns a tree for `urushi-render', or nil while
nothing is being composed.

What it returns is laid over the frame, wherever the frame is put, so
it is drawn over the text whether the text was built here or drawn by
the host from what Emacs said it drew."
  :type 'function)

(defcustom urushi-screen-window-function #'urushi-screen-window
  "Function that draws one window.
It takes the window and which one it is, counting from zero, which is
what names the parts of it that the host keeps between screens."
  :type 'function)

(defcustom urushi-screen-tab-line-function nil
  "Function that draws the tab line of a window, or nil to draw it as text.
It takes the window and its tab line, a line as `window-screen-rows'
reads it, and returns XAML to fill the room Emacs kept for it.

Where the screen is the picture Emacs drew (`urushi-screen-emacs'),
Emacs draws the tab line into that picture as well unless it is told
not to: set `host-draw-tab-lines' to nil beside this, or the line is
drawn under what this returns and sent every time it changes.

The function is given the window and the line, and returns XAML:
`urushi-tabs-tab-line' draws the tabs of `tab-line-mode' there.  How much
room that is stays Emacs's to say, by the face `tab-line'."
  :type '(choice (const :tag "As text" nil) function))

(defun urushi-screen-run (run height &optional spacing above)
  "Return RUN, one stretch of a line, as XAML, HEIGHT pixels tall.
SPACING of those pixels are space between lines rather than text, and
ABOVE of them are over the text; Emacs puts the text in what is left,
and paints the background over all of it.

A run is put where Emacs put it rather than after the one before it, so
that every stretch of the line stands where Emacs decided it stands and
nothing drifts.  Within a run the characters are spaced by hand, since
there XAML is laying them out and Emacs is not."
  (let* ((scale (float urushi-scale))
         (text (urushi-screen--set (plist-get run :text)))
         (width (/ (plist-get run :width) scale))
         (size (/ (or (urushi-screen--set (plist-get run :size))
                      (default-font-width))
                  scale))
         ;; The name comes from a symbol of Emacs's own, which carries
         ;; which charset it was found for; XAML wants the name alone.
         (family (if-let* ((named (urushi-screen--set (plist-get run :family))))
                     (substring-no-properties named)
                   (urushi-screen-font-family)))
         (background (urushi-screen-color (plist-get run :background))))
    (when text
      (urushi-screen--measure family size))
    (if (plist-get run :image)
        (urushi-screen--image run (or above 0))
      (urushi-screen--run run text family size width height spacing above background))))

(defun urushi-screen--hover (run)
  "Return what RUN looks like under the pointer, for the host to show.
Emacs says which text lights up under the pointer and what it turns
into; where the pointer is is the host\'s to know, and it shows it
itself rather than asking and waiting to be told to draw again.

The colours are carried on the element that draws the run, as
\"hover:FOREGROUND:BACKGROUND\"; either may be empty, and nothing is
carried where the run looks the same under the pointer as beside it."
  (let ((foreground (urushi-screen-color (plist-get run :hover-foreground)))
        (background (urushi-screen-color (plist-get run :hover-background))))
    (when (or foreground background)
      (list :Tag (format "hover:%s:%s" (or foreground "") (or background ""))))))

(defun urushi-screen--run (run text family size width height spacing above background)
  "Return RUN, TEXT drawn in FAMILY at SIZE, as `urushi-screen-run' does.
WIDTH, HEIGHT, SPACING, ABOVE and BACKGROUND are as it worked them out."
  (let ((scale (float urushi-scale)))
    (let* ((left (/ (plist-get run :x) scale))
           (top (or above 0))
           (hover (urushi-screen--hover run))
           (body (and text (urushi-screen--text
                            run text family size (/ width (length text))
                            (- height (or spacing 0))))))
      (cond
       ;; A background needs something to paint it, and a TextBlock
       ;; cannot; without one there is nothing for a Border to do.
       ((or background hover)
        `(Border :Canvas.Left ,left
                 :Width ,width
                 :Height ,height
                 ,@(and background (list :Background background))
                 ;; Something has to be painted for the pointer to be
                 ;; counted as over it, and for a colour to replace.
                 ,@(and hover (not background) (list :Background "Transparent"))
                 ,@hover
                 ,@(and body
                        (list (append body
                                      (list :Margin (format "0,%s,0,0" top)))))))
       (body (append (list (car body) :Canvas.Left left :Canvas.Top top) (cdr body)))
       (t `(Border :Canvas.Left ,left :Width ,width :Height ,height))))))

(defvar urushi-screen--image-files (make-hash-table :test #'equal)
  "The files images given as data were written to, keyed by the data.")

(defun urushi-screen--image-file (spec)
  "Return the file SPEC, an image spec, shows, or nil if there is none.
An image made from a file is that file.  One made from data in Lisp is
written to a file of its own the first time it is shown, since what
the host draws images with reads them from files."
  (let ((file (plist-get (cdr spec) :file))
        (data (plist-get (cdr spec) :data)))
    (cond
     (file (let ((found (image-search-load-path file)))
             (and found (file-readable-p found) (expand-file-name found))))
     ((stringp data)
      (or (gethash data urushi-screen--image-files)
          (let* ((type (or (plist-get (cdr spec) :type)
                           (image-type-from-data data)
                           'png))
                 (directory (expand-file-name "urushi-images" temporary-file-directory))
                 (file (expand-file-name (format "%s.%s" (secure-hash 'sha1 data) type)
                                         directory)))
            (unless (file-exists-p file)
              (make-directory directory t)
              (let ((coding-system-for-write 'no-conversion))
                (write-region data nil file nil 'silent)))
            (puthash data file urushi-screen--image-files)))))))

(defun urushi-screen--image (run above)
  "Return RUN, an image, as a XAML Image where Emacs put it.
ABOVE is how much space between lines there is over the line's text.
It stands on the line's baseline, as Emacs puts it, and is as big as
Emacs made it.  An image that has no file to read is left blank."
  (let* ((scale (float urushi-scale))
         (spec (plist-get run :image))
         (file (urushi-screen--image-file spec))
         (left (/ (plist-get run :x) scale))
         (width (/ (plist-get run :width) scale))
         (height (/ (or (plist-get run :height) 0) scale))
         (top (+ above (/ (max 0 (- (or (plist-get run :line-ascent) 0)
                                    (or (plist-get run :ascent) 0)))
                          scale))))
    (if (not file)
        `(Border :Canvas.Left ,left :Width ,width :Height ,height)
      (let ((uri (concat "file:///" (replace-regexp-in-string
                                     " " "%20" (subst-char-in-string ?\\ ?/ file)))))
        `(Image :Canvas.Left ,left
                :Canvas.Top ,top
                :Width ,width
                :Height ,height
                :Stretch "Fill"
                ,@(if (string-suffix-p ".svg" file t)
                      `((Image.Source (SvgImageSource :UriSource ,uri)))
                    `(:Source ,uri)))))))

(defun urushi-screen--text (run text family size advance height)
  "Return TEXT of RUN as a XAML TextBlock.
FAMILY, SIZE and ADVANCE are the font it is drawn in and how far apart
its characters go; HEIGHT is how tall the line is."
  (let ((foreground (urushi-screen-color (plist-get run :foreground)))
        (weight (urushi-screen--set (plist-get run :weight)))
        (slant (urushi-screen--set (plist-get run :slant))))
    `(TextBlock :Text ,(urushi-literal text)
                :TextWrapping "NoWrap"
                :FontFamily ,family
                :FontSize ,size
                :LineHeight ,height
                :LineStackingStrategy "BlockLineHeight"
                :CharacterSpacing ,(urushi-screen--spacing family size advance)
                ;; The characters Emacs put one to a column stay one to a
                ;; column.  Whether they join is Emacs's to decide, with
                ;; `auto-composition-mode', and a font that joins them
                ;; here would draw what Emacs did not lay out.
                :Typography.StandardLigatures nil
                :Typography.ContextualAlternates nil
                ,@(when foreground `(:Foreground ,foreground))
                ,@(when (memq weight '(bold semi-bold ultra-bold extra-bold))
                    '(:FontWeight "Bold"))
                ,@(when (memq slant '(italic oblique)) '(:FontStyle "Italic"))
                ,@(when (urushi-screen--set (plist-get run :underline))
                    '(:TextDecorations "Underline")))))

(defun urushi-screen-line (line)
  "Return LINE, one line of a window, as XAML.
Its key is where it is, which is what tells the host that a line it
already has is the same line: typing changes the line the point is on
and leaves every other one alone."
  (let* ((scale (float urushi-scale))
         (height (/ (plist-get line :height) scale))
         (spacing (/ (or (plist-get line :line-spacing) 0) scale))
         (above (/ (or (plist-get line :line-spacing-above) 0) scale)))
    `(Canvas :key ,(format "%s-%s" (plist-get line :kind) (plist-get line :y))
             :Canvas.Top ,(/ (plist-get line :y) scale)
             :Height ,height
             ,@(mapcar (lambda (run)
                         ;; An image goes on the line by its baseline,
                         ;; which is the line's to say.
                         (when (plist-get run :image)
                           (setq run (append run (list :line-ascent
                                                       (plist-get line :ascent)))))
                         (funcall urushi-screen-run-function run height spacing above))
                       (plist-get line :runs)))))

(defvar urushi-screen--composing ""
  "What the input method is turning over, or an empty string.
It is not in any buffer: the input method has not settled on it, and
Emacs will not see it until it does.  Drawing it is this file's to do,
because the window that would otherwise draw it cannot be seen.")

(defvar urushi-screen--composing-runs nil
  "How the input method marked each stretch of what it is composing.
A list of plists of :length, :underline and, where it asked for both of
them, :foreground and :background.  The line under a stretch is how an
input method says which part of the text it is working on now.")

(defvar urushi-screen--composing-caret 0
  "How far into what is being composed the input method put the caret.
In characters.  Which clause is being worked on shows there as well as
in the lines under the text.")

(defun urushi-screen--composing-underline (kind left top width height color)
  "Return the line to draw under a stretch of what is being composed.
KIND is what the host called it; the line goes at LEFT and TOP, is WIDTH
wide and HEIGHT tall, and is drawn in COLOR.

XAML underlines text one way only, so each kind is drawn as a shape of
its own under the text."
  (let ((thickness (max 1.0 (/ height 16.0))))
    (pcase kind
      ("none" nil)
      ("double"
       (list `(Rectangle :Canvas.Left ,left :Canvas.Top ,top
                         :Width ,width :Height ,thickness :Fill ,color)
             `(Rectangle :Canvas.Left ,left :Canvas.Top ,(+ top (* thickness 2))
                         :Width ,width :Height ,thickness :Fill ,color)))
      ("wavy"
       ;; A zigzag, which is what a wave comes to at this size.
       (let* ((step (max 2.0 (* thickness 2)))
              (points (let ((x 0.0) (up nil) (parts nil))
                        (while (<= x width)
                          (push (format "%s,%s" x (if up 0.0 step)) parts)
                          (setq up (not up))
                          (setq x (+ x step)))
                        (nreverse parts))))
         (list `(Polyline :Canvas.Left ,left :Canvas.Top ,top
                          :Stroke ,color :StrokeThickness ,thickness
                          :Points ,(mapconcat #'identity points " ")))))
      ((or "dotted" "dashed")
       (list `(Line :Canvas.Left ,left :Canvas.Top ,top
                    :X1 0 :Y1 0 :X2 ,width :Y2 0
                    :Stroke ,color :StrokeThickness ,thickness
                    :StrokeDashArray ,(if (equal kind "dotted") "1,2" "3,2"))))
      (_
       (list `(Rectangle :Canvas.Left ,left :Canvas.Top ,top
                         :Width ,width :Height ,thickness :Fill ,color))))))

(defun urushi-screen--default-size ()
  "Return the size the default font is drawn at, as a line's runs have it.
A run is drawn at the pixel size redisplay settled on, which is not what
the face's height comes to: what is being composed stands beside that
text and is drawn to match it, and is measured under the same name."
  (let ((font (urushi-screen--set (face-attribute 'default :font))))
    (/ (or (and (fontp font) (font-get font :size))
           (default-font-width))
       (float urushi-scale))))

(defvar urushi-screen--composing-families (make-hash-table :test #'eq)
  "The font each character of a composition is drawn in, by character.
Asking the fontset costs a font lookup, and what is being composed is
drawn again on every keystroke.")

(defun urushi-screen--composing-family (char)
  "Return the family Emacs would draw CHAR in.
What is being composed is in no buffer, so redisplay never said which
font each of its characters was found in; the fontset is asked instead.
Drawn in the default font throughout, kana would be left to whatever
the host found them in, at a width nothing here knows."
  (or (gethash char urushi-screen--composing-families)
      (puthash char
               (or (ignore-errors
                     (let ((font (car (internal-char-font nil char))))
                       (and font (urushi-screen--set (font-get font :family))
                            (symbol-name (font-get font :family)))))
                   (urushi-screen-font-family))
               urushi-screen--composing-families)))

(defun urushi-screen--composing-text (text left top height cell color)
  "Return TEXT as XAML, at LEFT and TOP, a column drawn CELL wide in COLOR.
HEIGHT is how tall a line is.

Split where the font changes and where the characters stop being the
same width, and spaced by hand within each stretch, as
`urushi-screen-run' spaces a line: XAML lays a stretch out at the widths
the font asks for, and Emacs laid it out on a grid of whole columns, so
the two drift apart across it.  Left alone, what is being composed comes
out a different width from the room made for it, and more so the longer
it grows, until the cursor at the end of it no longer stands there."
  (let ((size (urushi-screen--default-size))
        (at 0)
        (x left)
        (parts nil))
    (while (< at (length text))
      (let* ((columns (char-width (aref text at)))
             (family (urushi-screen--composing-family (aref text at)))
             (end (let ((i at))
                    (while (and (< i (length text))
                                (= (char-width (aref text i)) columns)
                                (equal (urushi-screen--composing-family (aref text i)) family))
                      (setq i (1+ i)))
                    i))
             (part (substring text at end))
             (advance (* columns cell)))
        (urushi-screen--measure family size)
        (push `(TextBlock :Text ,(urushi-literal part)
                          :Canvas.Left ,x
                          :Canvas.Top ,top
                          :TextWrapping "NoWrap"
                          :FontFamily ,family
                          :FontSize ,size
                          :LineHeight ,height
                          :LineStackingStrategy "BlockLineHeight"
                          :Foreground ,color
                          :CharacterSpacing ,(urushi-screen--spacing family size advance))
              parts)
        (setq x (+ x (* (string-width part) cell)))
        (setq at end)))
    (nreverse parts)))

(defun urushi-screen--composing-parts (left top height cell color background)
  "Return what is being composed, as XAML, laid out from LEFT and TOP.
HEIGHT is how tall a line is and CELL how wide one column is drawn; COLOR
is what the text is drawn in and BACKGROUND what is behind it.

Each stretch the input method marked is drawn where Emacs would put it,
with a line of its own kind under it, and the caret goes where the input
method left it."
  (let ((at 0)
        (x left)
        (parts nil)
        ;; What the host sent arrives as a vector, and there is nothing
        ;; when the input method marked nothing: one plain stretch then.
        (runs (or (append urushi-screen--composing-runs nil)
                  (list (list :length (length urushi-screen--composing)
                              :underline "solid"))))
        (caret-x nil))
    (dolist (run runs)
      (let* ((length (min (or (plist-get run :length) 0)
                          (- (length urushi-screen--composing) at)))
             (text (substring urushi-screen--composing at (+ at length)))
             (width (* (string-width text) cell)))
        (when (and (null caret-x) (<= urushi-screen--composing-caret (+ at length)))
          (setq caret-x
                (+ x (* (string-width
                         (substring text 0 (- urushi-screen--composing-caret at)))
                        cell))))
        ;; The line under the text is drawn over what the buffer has
        ;; there, so the room it takes is painted over first.
        (push `(Rectangle :Canvas.Left ,x :Canvas.Top ,top
                          :Width ,width :Height ,height
                          :Fill ,(or (urushi-screen-color (plist-get run :background))
                                     background))
              parts)
        (dolist (part (urushi-screen--composing-text
                       text x top height cell
                       (or (urushi-screen-color (plist-get run :foreground)) color)))
          (push part parts))
        (dolist (line (urushi-screen--composing-underline
                       (or (plist-get run :underline) "solid")
                       x (+ top height) width height color))
          (push line parts))
        (setq at (+ at length))
        (setq x (+ x width))))
    (nreverse (cons `(Rectangle :Canvas.Left ,(or caret-x x) :Canvas.Top ,top
                                :Width 2 :Height ,height :Fill ,color)
                    parts))))

(defvar urushi-screen--caret nil
  "Where the cursor last was, as (X Y WIDTH HEIGHT), or nil.
The host is told, so that the candidates of the input method appear
beside the text rather than in a corner.  What was told, and not where
the cursor is: see `urushi-screen--caret-at'.")

(defvar urushi-screen--caret-at nil
  "Where the cursor is on the screen now, as (X Y WIDTH HEIGHT), or nil.

Nil where the window has been scrolled away from it and there is no
cursor to be seen.  Nothing is drawn beside a cursor that is not there:
what was being composed would otherwise stay where the cursor was, over
whatever the window was scrolled to.")

(defun urushi-screen--tell-caret (x y width height)
  "Tell the host the cursor is at X, Y and is WIDTH by HEIGHT."
  (let ((caret (list x y width height)))
    (unless (equal caret urushi-screen--caret)
      (setq urushi-screen--caret caret)
      (urushi--send (list :type "caret" :x x :y y :width width :height height)))))

(defun urushi-screen--face-background (face frame)
  "Return the background FACE gives on FRAME, or nil where it gives none.
FACE is what `get-char-property' hands back: a face, a list of faces, or
a list of attributes."
  (cond ((null face) nil)
        ((keywordp (car-safe face)) (urushi-screen-color (plist-get face :background)))
        ((proper-list-p face)
         (seq-some (lambda (one) (urushi-screen--face-background one frame)) face))
        ((facep face) (urushi-screen-color (face-attribute face :background frame)))))

(defun urushi-screen--line-background (window)
  "Return the colour of the line WINDOW's cursor is on.
What is being composed is drawn over that line, so a box of the frame's
own background would cover what an overlay put there: the line
`hl-line-mode' marks would be the colour of every other one from the
first character being composed onwards."
  (let ((frame (window-frame window)))
    (or (urushi-screen--face-background
         (get-char-property (window-point window) 'face (window-buffer window))
         frame)
        (urushi-screen-color (face-attribute 'default :background frame t)))))

(defun urushi-screen-composing (frame)
  "Return what the input method is composing, to be laid over FRAME.

Laid over the frame and not put in a window: it belongs where the
cursor is and to no line of any window, and where the host draws what
Emacs said it drew there is no window built here to put it in.

Where the cursor is comes from `urushi-screen--caret-at', which is worked
out for every screen, so this is as far behind the cursor as the screen
is; nothing is returned while the window has been scrolled away from the
cursor and there is nowhere for this to go."
  (when-let* (((not (string-empty-p urushi-screen--composing)))
              (caret urushi-screen--caret-at)
              (window (frame-selected-window frame)))
    (let ((scale (float urushi-scale))
          (color (or (urushi-screen-color (face-attribute 'cursor :background frame t))
                     (urushi-screen-color (face-attribute 'default :foreground frame t)))))
      `(Canvas :key "composing"
               :IsHitTestVisible "False"
               ,@(urushi-screen--composing-parts
                  (/ (nth 0 caret) scale)
                  (/ (nth 1 caret) scale)
                  (/ (nth 3 caret) scale)
                  (/ (default-font-width) scale)
                  color
                  (urushi-screen--line-background window))))))

(defun urushi-screen--composing-over (frame)
  "Return the place what is being composed on FRAME is laid over it in.

Always there, and holding something only while something is being
composed: it is a row of its own, and a row is sent by itself.  Put in
the screen around the frame instead, the screen would be built again
for every keystroke of a conversion -- the whole of it, the element the
host draws the text in among it -- and the text would go out and come
back under what is being composed."
  (list `(Rows :key "composing" :panel "Canvas"
               ,@(when-let* ((composing (funcall urushi-screen-composing-function frame)))
                   (list composing)))))

(defun urushi-screen--caret-from (window)
  "Say where the cursor of WINDOW is, for the input method to ask about.

Said whether or not a cursor is drawn here, and so not left to whoever
draws one: where the host draws what Emacs says it drew, the cursor is
one of the things Emacs said and no cursor is built for the screen, and
the input method still has to know where what it is composing goes.
Nothing told it, and it put the composition in the corner of the
window."
  (setq urushi-screen--caret-at
        ;; From the cursor, wherever it has come to be: the wheel
        ;; scrolls by moving point, so a window scrolled away from what
        ;; is being composed has point, the cursor and the composition
        ;; all against the edge together.  That is what Emacs does on a
        ;; window system of its own, and what is settled on goes in
        ;; where the cursor has come to as well, so the two agree.
        (when-let* (((eq window (selected-window)))
                    (cursor (window-screen-cursor window)))
          (let ((spacing (or (plist-get cursor :line-spacing) 0))
                (above (or (plist-get cursor :line-spacing-above) 0))
                (origin (urushi-screen--window-origin window)))
            (list (+ (car origin) (plist-get cursor :x))
                  (+ (cdr origin) (plist-get cursor :y) above)
                  (plist-get cursor :width)
                  (- (plist-get cursor :height) spacing)))))
  ;; Told only where there is one.  A cursor that has been scrolled away
  ;; from is at no place to tell, and the last place told is the best
  ;; there is to leave the input method looking at.
  (when urushi-screen--caret-at
    (apply #'urushi-screen--tell-caret urushi-screen--caret-at)))

(defun urushi-screen-cursor (window)
  "Return the cursor of WINDOW, to be laid over its text.
It is laid over rather than put in a line, so that moving it leaves
every line as it was and a keystroke costs one line of the screen.

It is not drawn while Emacs has it hidden, which is half the time while
it blinks; what is being composed is drawn all the same."
  (when-let* (((eq window (selected-window)))
              (cursor (window-screen-cursor window))
              (frame (window-frame window))
              (color (or (urushi-screen-color (face-attribute 'cursor :background frame t))
                         (urushi-screen-color (face-attribute 'default :foreground frame t)))))
    ;; As tall as the text and where the text is: the space between lines
    ;; is not the line's text, and a cursor through it touches the next.
    (let* ((scale (float urushi-scale))
           (spacing (or (plist-get cursor :line-spacing) 0))
           (above (or (plist-get cursor :line-spacing-above) 0))
           (left (/ (plist-get cursor :x) scale))
           (top (/ (+ (plist-get cursor :y) above) scale))
           (height (/ (- (plist-get cursor :height) spacing) scale))
           ;; As wide as Emacs says: a bar is as wide as `cursor-type'
           ;; asks for and a box is as wide as the character under it,
           ;; and drawing every one of them two pixels wide draws a bar
           ;; where a box was meant.
           (width (/ (max (or (plist-get cursor :width) 0) 1) scale)))
      `(Canvas :key "cursor"
               :IsHitTestVisible "False"
               (Rectangle :Canvas.Left ,left
                          :Canvas.Top ,top
                          :Width ,width
                          :Height ,height
                          :Fill ,color
                          ;; While something is being composed it draws
                          ;; the cursor itself, where the input method
                          ;; put it rather than where the text will go.
                          ,@(unless (and (internal-show-cursor-p window)
                                         (string-empty-p urushi-screen--composing))
                              '(:Visibility "Collapsed")))
               ))))

;;;; The windows

(defvar urushi-screen--built (make-hash-table :test #'equal)
  "What each line of this screen was built into, keyed by the line.")

(defvar urushi-screen--built-before (make-hash-table :test #'equal)
  "The same, for the screen before, which is what this one reuses.")

(defun urushi-screen--line (line)
  "Return LINE built, reusing what it was built into last time.
A line Emacs drew the same way is the same line, and building it again
would only arrive at what is already here.  Handing back the very
object from last time is also what lets `urushi-render' know, without
looking, that there is nothing to send for it."
  (puthash line
           (or (gethash line urushi-screen--built-before)
               (funcall urushi-screen-line-function line))
           urushi-screen--built))

(defun urushi-screen--start-screen ()
  "Begin a screen, keeping only what the one before it built.
Two screens' worth is all that is ever reused, and holding more would
be holding every line the session has ever shown."
  (setq urushi-screen--built-before urushi-screen--built)
  (setq urushi-screen--built (make-hash-table :test #'equal)))

(defvar urushi-screen--said-nothing 0
  "How many times there has been no screen to read, and it was said.")

(defun urushi-screen--nothing-to-draw (window)
  "Say that WINDOW has no screen to read, and why it might not.
A window with nothing in it looks the same as one this cannot read, and
the difference is not something to find out twice."
  (when (< urushi-screen--said-nothing 5)
    (cl-incf urushi-screen--said-nothing)
    (urushi--log "no rows for %S: frame visible %S, size %Sx%S, cursor %S"
                window
                (frame-visible-p (window-frame window))
                (frame-pixel-width (window-frame window))
                (frame-pixel-height (window-frame window))
                (window-screen-cursor window))))

(defun urushi-screen-window (window index)
  "Return WINDOW as XAML, where it sits on the frame.
INDEX says which window this is, and names the parts of it the host
keeps between one screen and the next.

The lines and the cursor are each a set of rows of their own, so that a
line that has not changed is not sent again and the cursor can move
without any line being touched.

What is drawn is cut to the window, as Emacs cuts it: a line can be
taller than the window has room for, the echo area's when it shows
text in a taller font for one, and Emacs shows as much of it as fits."
  (let* ((scale (float urushi-scale))
         (rows (window-screen-rows window))
         ;; Its edges are inside the frame's border, where
         ;; `window-pixel-left' and `window-pixel-top' do not count it.
         (edges (window-pixel-edges window))
         (width (/ (window-pixel-width window) scale))
         (height (/ (window-pixel-height window) scale))
         (background (urushi-screen-color
                      (face-attribute 'default :background (window-frame window) t))))
    (unless rows
      (urushi-screen--nothing-to-draw window))
    `(Canvas :Canvas.Left ,(/ (nth 0 edges) scale)
             :Canvas.Top ,(/ (nth 1 edges) scale)
             :Width ,width
             :Height ,height
             ,@(when background `(:Background ,background))
             (Canvas.Clip
              (RectangleGeometry :Rect ,(format "0,0,%s,%s" width height)))
             (Rows :key ,(format "window-%d" index)
                   :panel "Canvas"
                   ,@(mapcar (lambda (row)
                               (if (and urushi-screen-tab-line-function
                                        (eq (plist-get row :kind) 'tab-line))
                                   (urushi-screen--tab-line window row width)
                                 (urushi-screen--line row)))
                             rows))
             (Rows :key ,(format "cursor-%d" index)
                   :panel "Canvas"
                   ,@(when-let* ((cursor (funcall urushi-screen-cursor-function
                                                  window)))
                       (list cursor)))
             ,@(urushi-screen--dividers window width height))))

(defun urushi-screen--dividers (window width height)
  "Return the dividers of WINDOW, which is WIDTH by HEIGHT, as Emacs draws them.
With `window-divider-mode', a window keeps a strip at its bottom and its
right for them, which is in its size and none of its lines."
  (let ((scale (float urushi-scale))
        (color (urushi-screen-color
                (face-attribute 'window-divider :foreground (window-frame window) t)))
        (bottom (window-bottom-divider-width window))
        (right (window-right-divider-width window)))
    (when color
      (append
       (when (< 0 bottom)
         `((Rectangle :Canvas.Top ,(- height (/ bottom scale))
                      :Width ,width :Height ,(/ bottom scale) :Fill ,color)))
       (when (< 0 right)
         `((Rectangle :Canvas.Left ,(- width (/ right scale))
                      :Width ,(/ right scale) :Height ,height :Fill ,color)))))))

(defun urushi-screen--tab-line (window line width)
  "Return LINE, the tab line of WINDOW, WIDTH wide.
It is drawn by `urushi-screen-tab-line-function', and not kept from
one screen to the next as a line of text is: what it draws depends on
the window, and two windows can have the same line."
  (let ((scale (float urushi-scale)))
    `(Grid :key ,(format "tab-line-%s" (plist-get line :y))
           :Canvas.Top ,(/ (plist-get line :y) scale)
           :Width ,width
           :Height ,(/ (plist-get line :height) scale)
           ,(funcall urushi-screen-tab-line-function window line))))

(defvar urushi-screen--windows-drawn 0
  "How many windows this screen has drawn so far.
It is what numbers them, so that no two share a name however many
frames they are spread over.")

(defun urushi-screen--frame-origin (frame)
  "Return where FRAME is, as (X . Y) pixels from its root frame's corner."
  (let ((x 0) (y 0))
    (while (frame-parent frame)
      (let ((position (frame-position frame)))
        (setq x (+ x (car position))
              y (+ y (cdr position))
              frame (frame-parent frame))))
    (cons x y)))

(defun urushi-screen--window-origin (window)
  "Return where WINDOW is, as (X . Y) pixels from its root frame's corner.
It is where its edges are, which are inside the frame's border:
`window-pixel-left' and `window-pixel-top' count from inside it."
  (let ((frame (urushi-screen--frame-origin (window-frame window)))
        (edges (window-pixel-edges window)))
    (cons (+ (car frame) (nth 0 edges))
          (+ (cdr frame) (nth 1 edges)))))

(defun urushi-screen--child-frames (frame)
  "Return the child frames of FRAME that can be seen, the lowest first.
That is the order to draw them in, each over the ones before it, as Emacs
stacks them: the one shown last, or raised, on top, and the shadow of
one under it falling behind it rather than on it."
  (cl-remove-if-not (lambda (child)
                      (and (eq (frame-parent child) frame)
                           (eq (frame-visible-p child) t)
                           ;; The frame of a panel is drawn in its panel.
                           (not (frame-parameter child 'urushi-panel))))
                    (reverse (frame-list-z-order frame))))

(defun urushi-screen--border-color (frame)
  "Return the colour of the border around FRAME, or nil for none.
A child frame's is the face `child-frame-border', where there is one,
and any frame's otherwise is the face `internal-border'."
  (cl-loop for face in '(child-frame-border internal-border)
           thereis (and (facep face)
                        (urushi-screen-color
                         (face-attribute face :background frame t)))))

(defun urushi-screen--frame-name (frame)
  "Return a name for FRAME that no other frame has while it lives."
  (format "%x" (sxhash-eq frame)))

(defcustom urushi-screen-child-frame-function #'urushi-screen-child-frame-body
  "Function that draws one child frame, such as a floating minibuffer.
It takes the frame and returns it as XAML, as big as the frame is;
where it goes on its parent is Emacs's to decide, and it is put there.
The one that draws it as Emacs would is `urushi-screen-child-frame-body',
which one of your own can wrap in whatever it likes: a shadow under
it, rounded corners, only for some frames and not others."
  :type 'function)

(cl-defun urushi-screen-child-frame-body (frame &key (border t) corner-radius)
  "Return FRAME, a child frame, as XAML, the way Emacs draws one.
That is its own background, the border around it, and its windows, with
its own children over those.

BORDER nil leaves the border out, for a frame that has something else
to set it off, a shadow for one; the room Emacs keeps for it is then
the frame's background.  CORNER-RADIUS rounds the corners of the frame,
the border with them, and cuts off what is drawn in them."
  (let* ((scale (float urushi-scale))
         (width (/ (frame-native-width frame) scale))
         (height (/ (frame-native-height frame) scale))
         (thickness (frame-internal-border-width frame))
         (border-color (and border (urushi-screen--border-color frame)))
         (background (urushi-screen-color
                      (face-attribute 'default :background frame t))))
    ;; The border is laid over the windows rather than drawn by the Border
    ;; around them, which would move them in by its thickness: where Emacs
    ;; put a window counts from the corner of the frame, border and all.
    `(Border :Width ,width
             :Height ,height
             ,@(when corner-radius `(:CornerRadius ,corner-radius))
             ,@(when background `(:Background ,background))
             (Grid
              ,(funcall urushi-screen-frame-function frame)
              ,@(when (and border-color (< 0 thickness))
                  `((Border :BorderThickness ,(/ thickness scale)
                            :BorderBrush ,border-color
                            ,@(when corner-radius `(:CornerRadius ,corner-radius))
                            :IsHitTestVisible nil)))))))

(defun urushi-screen-child-frame (frame)
  "Return FRAME, a child frame, as a row of XAML, where it sits on its parent.
What is drawn is up to `urushi-screen-child-frame-function'.

It is a row of its own, and its windows are rows inside it, so that a
child frame that comes, goes or changes size is sent by itself: the
frame it is over stays as it was, and so do its own lines when only
where it is has changed."
  (let ((scale (float urushi-scale))
        (position (frame-position frame)))
    `(Canvas :key ,(urushi-screen--frame-name frame)
             :Canvas.Left ,(/ (car position) scale)
             :Canvas.Top ,(/ (cdr position) scale)
             ,(funcall urushi-screen-child-frame-function frame))))

(defun urushi-screen-windows (frame)
  "Return every window of FRAME, each where it is.
The echo area is among them: it is the minibuffer window, and Emacs
draws what it has to say there like anything else.

The child frames of FRAME that can be seen are drawn over its windows,
where Emacs put them: completion that pops up by the point, or a
minibuffer that floats in the middle of the frame."
  `(Canvas ,@(cl-loop for window in (window-list frame t)
                      collect (funcall urushi-screen-window-function
                                       window
                                       (1- (cl-incf urushi-screen--windows-drawn))))
           ;; There whether or not there are any, so that one coming or
           ;; going changes what is in it and nothing around it.
           (Rows :key ,(concat "frames-" (urushi-screen--frame-name frame))
                 :panel "Canvas"
                 ,@(mapcar #'urushi-screen-child-frame
                           (urushi-screen--child-frames frame)))))

(defun urushi-screen--tab-lines (frame)
  "Return the tab lines of FRAME's windows, drawn in the room Emacs kept.

Emacs keeps room for the tab line at the top of a window; where these
fill it, Emacs is told to leave that room alone rather than draw the
line into the picture under them.

Nothing unless `urushi-screen-tab-line-function' says to draw them: it
is what turns the line Emacs would draw into elements of its own."
  (when urushi-screen-tab-line-function
    (let ((scale (float urushi-scale))
          (index 0))
      (delq nil
            (mapcar
             (lambda (window)
               (let ((at (cl-incf index))
                     (edges (window-pixel-edges window))
                     (line (seq-find (lambda (row)
                                       (eq (plist-get row :kind) 'tab-line))
                                     (window-screen-rows window))))
                 (when line
                   `(Canvas :key ,(format "tab-line-%d" at)
                            :Canvas.Left ,(/ (nth 0 edges) scale)
                            :Canvas.Top ,(/ (nth 1 edges) scale)
                            ,(urushi-screen--tab-line
                              window line
                              (/ (window-pixel-width window) scale))))))
             (window-list frame t))))))

(defun urushi-screen-emacs (frame)
  "Return the room the screen Emacs draws for FRAME is shown in.

Emacs reads the font files and rasterizes the glyphs itself, so it lays
the text out and draws it by the same measurements; the host is handed
that picture and shows it here, under whatever else this holds.

What is in the picture is everything Emacs draws -- the text, the mode
line, the header line, the fringes and the cursor -- so anything else
on the screen is a component beside this one, and anything of Emacs\\='s
that is to be one is turned off in Emacs first.

The tab line of a window is drawn here rather than in the picture
where `urushi-screen-tab-line-function' says to; see it for what to
tell Emacs beside that.  A child frame is drawn here too, over
everything, where Emacs floated one on this frame.

Put in `urushi-screen-components' in place of `urushi-screen-windows',
which builds the same screen out of elements of its own instead."
  ;; A background, transparent though it is, is what makes XAML count
  ;; the pointer as being over the frame; the picture itself is not
  ;; there to be pointed at.
  ;;
  ;; As tall as the room the frame is shown in, which is less than the
  ;; frame itself wherever the frame reaches past it to put the echo
  ;; area out of sight: the host shows the picture within this element,
  ;; so what is past it is not seen.
  `(Canvas :Name ,(concat "urushi-emacs:" (urushi-screen--frame-name frame))
           :Background "Transparent"
           :Margin ,(format "0,0,0,%s" (urushi-screen--echo-area-height frame))
           ;; In the room Emacs keeps for a tab line, which Emacs is
           ;; told to leave alone where these fill it.
           (Rows :key ,(concat "tab-lines-" (urushi-screen--frame-name frame))
                 :panel "Canvas"
                 ,@(urushi-screen--tab-lines frame))
           ;; There whether or not there are any, so that a child frame
           ;; coming or going changes what is in it and nothing around
           ;; it.  Last, so that one floats over everything else.
           (Rows :key ,(concat "frames-" (urushi-screen--frame-name frame))
                 :panel "Canvas"
                 ,@(mapcar #'urushi-screen-child-frame
                           (urushi-screen--child-frames frame)))))

(put 'urushi-screen-emacs 'urushi-screen-frame t)

(defcustom urushi-screen-frame-function #'urushi-screen-windows
  "Function that fills the room the Emacs frame is given.
It takes the frame and returns a tree for `urushi-render'.

`urushi-screen-windows' builds the screen out of elements, reading what
Emacs laid out; `urushi-screen-emacs' leaves the room for the picture
Emacs drew itself and lets the host show that there.

This is what `urushi-screen-frame-site' puts in the frame's element,
which is how a layout places the frame.  A screen built by listing
components in `urushi-screen-components' instead names one of the two
there."
  :type 'function)

;;;; The screen

(defcustom urushi-screen-components '(urushi-screen-windows)
  "Functions that build the screen.
Each takes the frame being shown and returns a tree for `urushi-render',
or nil for nothing at all.  They are passed in this order to
`urushi-screen-layout-function'.

The one whose symbol has a non-nil `urushi-screen-frame' property is
where the Emacs frame goes, and takes whatever room the others leave;
see `urushi-screen-layout'.  One with a non-nil `urushi-screen-fill'
property takes that room too, and puts the frame somewhere in it
itself, with `urushi-screen-frame-site', as `urushi-layout' does.

A component can draw the window's title bar.  An element named
urushi-titlebar is what tells the host to take the window's own title
bar away and to let that element move the window, as a title bar does;
the controls on it are left to be clicked.  Without one, the window
keeps the title bar Windows gives it."
  :type '(repeat function))

(put 'urushi-screen-windows 'urushi-screen-frame t)

(defcustom urushi-screen-layout-function #'urushi-screen-layout
  "Function that puts the built components together into one tree.
It takes the list of what the components returned, in order, and the
frame they were built for."
  :type 'function)

(defun urushi-screen--frame-part-p (part)
  "Return non-nil if PART is where the Emacs frame goes."
  (get (car part) 'urushi-screen-frame))

(defun urushi-screen--fill-part-p (part)
  "Return non-nil if PART takes the room the other parts leave."
  (or (urushi-screen--frame-part-p part)
      (get (car part) 'urushi-screen-fill)))

(defvar urushi-screen--sites (make-hash-table :test #'equal)
  "The frame shown where each name says, other than the root frame's.")

(defcustom urushi-screen-echo-area t
  "Whether the echo area at the bottom of the frame is shown.
t shows it, as Emacs does.  `when-active' shows it only while the
minibuffer is being typed in there, and leaves it out otherwise, for a
screen that says what Emacs says somewhere else, a status bar with
`urushi-statusbar-message' in it for one.

Emacs keeps the line whether or not it is shown, so leaving it out
makes the frame taller than the room it has by that line, and the line
goes past the bottom, under whatever is there."
  :type '(choice (const :tag "Shown" t)
                 (const :tag "Only while typed in" when-active)))

(defun urushi-screen--echo-area-height (frame)
  "Return how far FRAME reaches past its room to put the echo area out of sight.
That is zero unless `urushi-screen-echo-area' says to leave it out, and
zero while the minibuffer is being typed in there."
  (let ((window (minibuffer-window frame)))
    (if (and (eq urushi-screen-echo-area 'when-active)
             (window-live-p window)
             (eq (window-frame window) frame)
             (not (eq window (active-minibuffer-window))))
        (/ (window-pixel-height window) (float urushi-scale))
      0)))

(defun urushi-screen--echo-area-margin (frame)
  "Return the properties that put the echo area of FRAME out of sight."
  (let ((height (urushi-screen--echo-area-height frame)))
    (unless (zerop height)
      `(:Margin ,(format "0,0,0,%s" (- height))))))

(defun urushi-screen-frame-site (frame &optional name)
  "Return the windows of FRAME in the element the host sizes the frame by.
It is named urushi-frame: the host makes the frame as big as it is, so
what else is on the screen is room the frame does not have, and Emacs
lays its text out to fit.  There is to be one of it on the screen.

A frame of its own shown somewhere else on the screen, as the frame of
a panel is, is given a NAME, a string, and is put in an element named
urushi-frame:NAME, which the host sizes it by in the same way.  The
element carries the number of the frame's window, which the host sends
the mouse to."
  (if (not name)
      ;; A background, transparent though it is, is what makes XAML
      ;; count the pointer as being over the frame: without one it is
      ;; over the letters alone, and what the frame is told of the
      ;; pointer is what is told to Emacs.
      `(Grid :Name "urushi-frame"
             :Background "Transparent"
             ,@(urushi-screen--echo-area-margin frame)
             ,(funcall urushi-screen-frame-function frame)
             ,@(urushi-screen--composing-over frame))
    (puthash name frame urushi-screen--sites)
    `(Grid :Name ,(concat "urushi-frame:" name)
           :Tag ,(frame-parameter frame 'window-id)
           :Background "Transparent"
           ,(funcall urushi-screen-frame-function frame))))

(defun urushi-screen--in-row (tree row)
  "Return TREE placed in ROW of the grid around it."
  (cons (car tree) (append (list :Grid.Row row) (cdr tree))))

(defun urushi-screen-layout (parts frame)
  "Return PARTS from top to bottom, on the background of `default'.
PARTS is an alist of the component that built each one and what it
built.

The part where the Emacs frame goes takes whatever room the others
leave, and is put in an element named urushi-frame: the host makes the
frame as big as that element, so what else is on the screen is room
the frame does not have, and Emacs lays its text out to fit.  The other
parts take the room they need."
  (let ((background (urushi-screen-color (face-attribute 'default :background frame t))))
    `(Grid ,@(when background `(:Background ,background))
           (Grid.RowDefinitions
            ,@(mapcar (lambda (part)
                        `(RowDefinition :Height ,(if (urushi-screen--fill-part-p part)
                                                     "*"
                                                   "Auto")))
                      parts))
           ,@(cl-loop for part in parts
                      for row from 0
                      collect (if (urushi-screen--frame-part-p part)
                                  `(Grid :Name "urushi-frame" :Grid.Row ,row
                                         ,@(urushi-screen--echo-area-margin frame)
                                         ,(cdr part)
                                         ,@(urushi-screen--composing-over frame))
                                (urushi-screen--in-row (cdr part) row))))))

(defun urushi-screen-tree (&optional frame)
  "Return the whole screen of FRAME as a tree for `urushi-render'."
  (urushi-screen--start-screen)
  (setq urushi-screen--windows-drawn 0)
  ;; Said for every screen, and not by whoever draws a cursor: where the
  ;; host draws what Emacs says it drew, nothing here draws one, and the
  ;; input method still has to be told where the text it is composing
  ;; goes.  Emacs has redisplayed by now, so the cursor is where it will
  ;; be seen.
  (urushi-screen--caret-from (selected-window))
  (let* ((frame (or frame (urushi-root-frame)))
         (parts (delq nil
                      (mapcar (lambda (component)
                                (when-let* ((tree (funcall component frame)))
                                  (cons component tree)))
                              urushi-screen-components))))
    (funcall urushi-screen-layout-function parts frame)))

(defvar urushi-screen--timed 0
  "How many screens have been timed, of `urushi-screen-timings'.")

(defcustom urushi-screen-timings 30
  "How many of the first screens to say how long they took.
Long enough to see what a keystroke costs, and then quiet."
  :type 'integer)

(defvar urushi-screen--rooms (make-hash-table :test #'eq)
  "How much room the host has for each frame, as (WIDTH . HEIGHT) in pixels.")

(defvar urushi-screen--rendering nil
  "Non-nil while the screen is being built.
Building it has Emacs redisplay first, and that is not a change to show.")

(defun urushi-screen-render ()
  "Show the screen in the host window."
  (interactive)
  (let ((urushi-screen--rendering t))
    (urushi-screen--render)))

(defun urushi-screen--render ()
  "Show the screen in the host window, saying how long it took at first."
  (maphash (lambda (frame _room) (urushi-screen--fit-frame frame))
           urushi-screen--rooms)
  (if (<= urushi-screen-timings urushi-screen--timed)
      (progn (redisplay)
             (urushi-render (urushi-screen-tree)))
    (cl-incf urushi-screen--timed)
    (let* ((start (current-time))
           ;; What is drawn is read out of the screen Emacs drew, so
           ;; there has to be one, and it has to be of what the buffers
           ;; hold now.
           (_ (redisplay))
           (drawn (current-time))
           (tree (urushi-screen-tree))
           (built (current-time))
           (sent (urushi-render tree)))
      (urushi--log "screen %d: redisplay %.1fms, build %.1fms, send %.1fms, %s"
                  urushi-screen--timed
                  (* 1000 (float-time (time-subtract drawn start)))
                  (* 1000 (float-time (time-subtract built drawn)))
                  (* 1000 (float-time (time-since built)))
                  sent))))

(defvar urushi-screen--pending nil
  "Timer that will show the screen, when one is waiting to run.")

(defun urushi-screen--redisplaying (windows)
  "Draw again once Emacs has redisplayed WINDOWS, if it redisplayed any.
What changes the screen is not always a command: a timer shows a popup,
the output of a process arrives in a buffer that is being shown.  Emacs
says which windows it is about to redisplay, and nil when there are
none, which is how often it is asked to with nothing to show for it.

A window on a frame that is not visible is left out: nothing on the
screen changes with it.  Building the screen marks such a window, the
minibuffer window of a hidden frame, when it selects a window on
another frame to read its tabs, and drawing again for it would build
the screen again, and mark it again, for as long as Emacs is idle."
  (when (and windows
             (not urushi-screen--rendering)
             (or (eq windows t)
                 (seq-some (lambda (window)
                             (eq (frame-visible-p (window-frame window)) t))
                           windows)))
    (urushi-screen--after-command)))

(defun urushi-screen--cursor-shown (&rest _)
  "Draw again now that the cursor has been hidden or shown.
This is how the cursor blinks: a timer hides it and shows it again, with
no command between, so nothing else would draw it."
  (urushi-screen--after-command))

(defun urushi-screen--after-command ()
  "Show the screen once what is happening now has finished happening.

Not while there is more input waiting.  What the input method settled on
arrives as its characters, each of them a command of its own, and a
screen between them shows the text being typed again letter by letter.
Each of those commands puts this back, so the screen is drawn once the
last of them has run."
  (unless urushi-screen--pending
    (setq urushi-screen--pending
          (urushi-when-idle
           (lambda ()
             (setq urushi-screen--pending nil)
             (unless (or unread-command-events (input-pending-p))
               (condition-case err
                   (urushi-screen-render)
                 (error (urushi--log "screen: %S" err)))))))))

;;;###autoload
(define-minor-mode urushi-screen-mode
  "Show the Emacs screen in the host window, as XAML."
  :global t
  (if urushi-screen-mode
      (progn
        (add-hook 'post-command-hook #'urushi-screen--after-command)
        (add-hook 'urushi-after-event-hook #'urushi-screen--after-command)
        (advice-add 'internal-show-cursor :after #'urushi-screen--cursor-shown)
        (add-function :before pre-redisplay-function #'urushi-screen--redisplaying)
        (add-hook 'urushi-stale-hook #'urushi-screen-render)
        (add-hook 'urushi-message-hook #'urushi-screen--message)
        (add-hook 'urushi-host-event-functions #'urushi-screen--window-changed)
        (urushi-forget)
        ;; Nothing has been displayed yet when this runs during startup,
        ;; so Emacs has no screen to tell about; ask again once it has.
        (urushi-screen--after-command)
        (urushi--log "screen mode on"))
    (remove-hook 'post-command-hook #'urushi-screen--after-command)
    (remove-hook 'urushi-after-event-hook #'urushi-screen--after-command)
    (advice-remove 'internal-show-cursor #'urushi-screen--cursor-shown)
    (remove-function pre-redisplay-function #'urushi-screen--redisplaying)
    (remove-hook 'urushi-stale-hook #'urushi-screen-render)
    (remove-hook 'urushi-message-hook #'urushi-screen--message)
    (remove-hook 'urushi-host-event-functions #'urushi-screen--window-changed)))

(defun urushi-screen--fit-frame (frame)
  "Make FRAME as big as the room the host has for it, if it is not.
Return non-nil if it had to be resized.

The room is for the whole frame, fringes and all, but `set-frame-size'
sizes only the part of it text goes in: what is around that is taken
off first.  What is around it can change afterwards, as when the
fringes are made wider, and Emacs then keeps the text as big as it was
and makes the frame bigger, past the room it has; which is why this is
asked again every time the screen is drawn."
  (when-let* (((frame-live-p frame))
              (room (gethash frame urushi-screen--rooms))
              ((not (and (= (car room) (frame-native-width frame))
                         (= (cdr room) (frame-native-height frame))))))
    (set-frame-size frame
                    (- (car room) (- (frame-native-width frame) (frame-text-width frame)))
                    (- (cdr room) (- (frame-native-height frame) (frame-text-height frame)))
                    t)
    t))

(defun urushi-screen--resize (message)
  "Lay the frame out to the size the host says it has room for.

The frame's own window is on no screen and its size means nothing to
Windows, so the size comes as a message rather than as a window being
resized.  Which also means it cannot come back: nothing here moves a
window that something else would then tell us about."
  (let* ((width (plist-get message :width))
         (height (plist-get message :height))
         (name (plist-get message :frame))
         (frame (if name
                    (gethash name urushi-screen--sites)
                  (urushi-root-frame))))
    (when (and (frame-live-p frame)
               (numberp width) (numberp height) (< 0 width) (< 0 height))
      (puthash frame (cons (truncate width) (truncate height)) urushi-screen--rooms)
      (urushi-screen--fit-frame frame)
      (urushi-forget)
      (urushi-screen-render))))

(defun urushi-screen--window-changed (event _message)
  "Draw again when EVENT changes the window in a way the screen shows.
A maximize button that restores once the window is maximized, or colours
chosen for a light window when Windows has turned dark."
  (when (memq event '(state theme))
    (urushi-screen--after-command)))

(defun urushi-screen--message (message)
  "Answer MESSAGE from the host, if it is this file's to answer."
  (pcase (plist-get message :type)
    ("measured" (urushi-screen--measured message))
    ("resize" (urushi-screen--resize message))
    ("composition"
     (setq urushi-screen--composing (or (plist-get message :text) ""))
     (setq urushi-screen--composing-runs (plist-get message :runs))
     (setq urushi-screen--composing-caret (or (plist-get message :caret) 0))
     (if (string-empty-p urushi-screen--composing)
         ;; Gone because it was settled on, as often as not, and what
         ;; it settled on is still to be put in: drawn now, the text
         ;; would vanish and come back.  Drawn once that is done.
         (urushi-screen--after-command)
       (urushi-screen-render)))
    ("commit"
     (urushi-screen--commit (or (plist-get message :text) "")))
    ;; The host has come by something it had not when the screen was
    ;; drawn -- the file of a font, which it asks for and is sent while
    ;; the screen goes on being drawn without it -- so what was left out
    ;; then is to be said again.  Everything, because what was left out
    ;; is whatever was in that font, and Emacs holds no note of it.
    ("redraw" (redraw-display))))

(defun urushi-screen--commit (text)
  "Take TEXT, what the input method settled on, as typed.
The characters go in as events, as keys would, so that whatever reads
the keys reads them: a minibuffer, isearch, a key bound to a character.
They go in together, ahead of any other input, and so are all in
before the screen is drawn again."
  (setq unread-command-events
        (append unread-command-events (string-to-list text))))

(provide 'urushi-screen)
;;; urushi-screen.el ends here
