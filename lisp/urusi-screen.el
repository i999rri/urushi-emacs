;;; urusi-screen.el --- The Emacs screen, built as XAML  -*- lexical-binding: t; -*-

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
;; returning a tree for `urusi-render':
;;
;;   `urusi-screen-windows'  every window of the frame, where it is
;;
;; Take one out of `urusi-screen-components' and it is gone.  Write your
;; own and it is there.  Wrap one and it keeps its contents:
;;
;;   (defun my-rounded-windows (frame)
;;     `(Border :CornerRadius 8 :Padding 12 :Background "#1b1b1f"
;;              ,(urusi-screen-windows frame)))
;;   (setq urusi-screen-components '(my-rounded-windows))
;;
;; Inside that, each piece is built by a function of its own, so that
;; changing one changes that much and no more:
;; `urusi-screen-window-function' for a window, `-line-function' for one
;; of its lines, `-run-function' for a stretch of a line drawn one way,
;; `-cursor-function' for the cursor, and `-child-frame-function' for a
;; child frame, such as a minibuffer floating over the frame.

;;; Code:

(require 'cl-lib)
(require 'subr-x)
(require 'urusi)

(defgroup urusi-screen nil
  "The Emacs screen, built as XAML."
  :group 'urusi)

(defcustom urusi-screen-font "Consolas"
  "Font to draw in when Emacs names none for a run.
That is so on a terminal, where Emacs never asked for one."
  :type 'string)

;;;; Faces and colours

(defun urusi-screen--set (value)
  "Return VALUE, or nil if what it came from said nothing about it."
  (unless (eq value 'unspecified) value))

(defvar urusi-screen--colors (make-hash-table :test #'equal)
  "What each colour Emacs named comes to in XAML.
Looking a colour up is a search through a table of names, and a screen
asks after the same handful of them hundreds of times.")

(defun urusi-screen-color (color)
  "Return COLOR as the #rrggbb XAML wants, or nil if there is no such color."
  (if (not (stringp (urusi-screen--set color)))
      nil
    (let ((known (gethash color urusi-screen--colors 'unknown)))
      (if (not (eq known 'unknown))
          known
        (puthash color
                 (when-let* ((values (color-values color)))
                   (apply #'format "#%02x%02x%02x"
                          (mapcar (lambda (v) (/ v 256)) values)))
                 urusi-screen--colors)))))

(defun urusi-screen-font-family ()
  "Return the family of the default font, as a name XAML knows."
  (let ((family (urusi-screen--set (face-attribute 'default :family))))
    (if (and (stringp family) (not (equal family "default")))
        family
      urusi-screen-font)))

(defun urusi-screen-font-size ()
  "Return the size of the default font, in the pixels XAML counts in."
  (let ((height (urusi-screen--set (face-attribute 'default :height))))
    ;; A height is tenths of a point on a frame that has a font, and a
    ;; multiplier on one that has none.  XAML counts 96ths of an inch.
    (if (and (integerp height) (<= 10 height))
        (/ (* height 96.0) 720.0)
      14.0)))

;;;; What the host measured

(defvar urusi-screen--advance (make-hash-table :test #'equal)
  "How wide the host draws a character, as a table of fonts.
The key is (FAMILY . SIZE) and the value (NARROW . WIDE).

Emacs lays its text out on a grid of whole columns -- one for most
characters, two for the likes of kana -- while the host draws at the
width the font asks for, so the two drift apart across a line.  How
wide the host draws one of each is what tells Emacs how much to correct
for; see `urusi-screen--spacing'.")

(defvar urusi-screen--asked nil
  "Fonts the host is measuring and has not answered for yet.")

(defun urusi-screen--font (family size)
  "Return what names FAMILY at SIZE in `urusi-screen--advance'.
The size is rounded because it is asked for and answered across JSON,
and a hundredth of a pixel apart is the same font."
  (cons family (round (* 100 size))))

(defun urusi-screen--measure (family size)
  "Ask the host how wide it draws FAMILY at SIZE, if it has not said."
  (let ((font (urusi-screen--font family size)))
    (unless (or (gethash font urusi-screen--advance)
                (member font urusi-screen--asked))
      (push font urusi-screen--asked)
      (urusi--send (list :type "measure" :family family :size size)))))

(defun urusi-screen--measured (message)
  "Take what the host measured in MESSAGE, and draw again knowing it."
  (let ((font (urusi-screen--font (plist-get message :family)
                                  (plist-get message :size))))
    (setq urusi-screen--asked (delete font urusi-screen--asked))
    (puthash font
             (cons (plist-get message :narrow) (plist-get message :wide))
             urusi-screen--advance)
    (urusi--log "font %s at %s drawn %s/%s, cell %s"
                (car font) (cdr font)
                (plist-get message :narrow) (plist-get message :wide)
                (default-font-width))
    (urusi-forget)
    (urusi-screen-render)))

(defun urusi-screen--spacing (family size advance)
  "Return what to add to each character of a run to space it as Emacs did.
FAMILY and SIZE are the font it is drawn in and ADVANCE how far apart
Emacs put its characters, both in the pixels XAML counts in.  XAML
counts the answer in thousandths of the font size."
  (let* ((measured (gethash (urusi-screen--font family size)
                            urusi-screen--advance))
         (cell (/ (default-font-width) (float urusi-scale)))
         ;; Which of the two the run is drawn with is decided by how
         ;; much room Emacs made for it, as Emacs decided it there too.
         (drawn (and measured
                     (if (< advance (* 1.5 cell)) (car measured) (cdr measured)))))
    (if (and (numberp drawn) (< 0 drawn) (< 0 size))
        (round (* 1000 (/ (- advance drawn) size)))
      0)))

;;;; The pieces

(defcustom urusi-screen-run-function #'urusi-screen-run
  "Function that draws one stretch of a line.
It takes the run, as `window-screen-rows' gives it, how tall the line is
in the pixels XAML counts in, and how much of that is space between
lines and how much of the space is above the text, in the same pixels.
See `urusi-screen-run'."
  :type 'function)

(defcustom urusi-screen-line-function #'urusi-screen-line
  "Function that draws one line of a window.
It takes the line, as `window-screen-rows' gives it."
  :type 'function)

(defcustom urusi-screen-cursor-function #'urusi-screen-cursor
  "Function that draws the cursor of a window, or nothing.
It takes the window."
  :type 'function)

(defcustom urusi-screen-window-function #'urusi-screen-window
  "Function that draws one window.
It takes the window and which one it is, counting from zero, which is
what names the parts of it that the host keeps between screens."
  :type 'function)

(defun urusi-screen-run (run height &optional spacing above)
  "Return RUN, one stretch of a line, as XAML, HEIGHT pixels tall.
SPACING of those pixels are space between lines rather than text, and
ABOVE of them are over the text; Emacs puts the text in what is left,
and paints the background over all of it.

A run is put where Emacs put it rather than after the one before it, so
that every stretch of the line stands where Emacs decided it stands and
nothing drifts.  Within a run the characters are spaced by hand, since
there XAML is laying them out and Emacs is not."
  (let* ((scale (float urusi-scale))
         (text (urusi-screen--set (plist-get run :text)))
         (width (/ (plist-get run :width) scale))
         (size (/ (or (urusi-screen--set (plist-get run :size))
                      (default-font-width))
                  scale))
         ;; The name comes from a symbol of Emacs's own, which carries
         ;; which charset it was found for; XAML wants the name alone.
         (family (if-let* ((named (urusi-screen--set (plist-get run :family))))
                     (substring-no-properties named)
                   (urusi-screen-font-family)))
         (background (urusi-screen-color (plist-get run :background))))
    (when text
      (urusi-screen--measure family size))
    (let* ((left (/ (plist-get run :x) scale))
           (top (or above 0))
           (body (and text (urusi-screen--text
                            run text family size (/ width (length text))
                            (- height (or spacing 0))))))
      (cond
       ;; A background needs something to paint it, and a TextBlock
       ;; cannot; without one there is nothing for a Border to do.
       (background `(Border :Canvas.Left ,left
                            :Width ,width
                            :Height ,height
                            :Background ,background
                            ,@(and body
                                   (list (append body
                                                 (list :Margin (format "0,%s,0,0" top)))))))
       (body (append (list (car body) :Canvas.Left left :Canvas.Top top) (cdr body)))
       (t `(Border :Canvas.Left ,left :Width ,width :Height ,height))))))

(defun urusi-screen--text (run text family size advance height)
  "Return TEXT of RUN as a XAML TextBlock.
FAMILY, SIZE and ADVANCE are the font it is drawn in and how far apart
its characters go; HEIGHT is how tall the line is."
  (let ((foreground (urusi-screen-color (plist-get run :foreground)))
        (weight (urusi-screen--set (plist-get run :weight)))
        (slant (urusi-screen--set (plist-get run :slant))))
    `(TextBlock :Text ,text
                :TextWrapping "NoWrap"
                :FontFamily ,family
                :FontSize ,size
                :LineHeight ,height
                :LineStackingStrategy "BlockLineHeight"
                :CharacterSpacing ,(urusi-screen--spacing family size advance)
                ,@(when foreground `(:Foreground ,foreground))
                ,@(when (memq weight '(bold semi-bold ultra-bold extra-bold))
                    '(:FontWeight "Bold"))
                ,@(when (memq slant '(italic oblique)) '(:FontStyle "Italic"))
                ,@(when (urusi-screen--set (plist-get run :underline))
                    '(:TextDecorations "Underline")))))

(defun urusi-screen-line (line)
  "Return LINE, one line of a window, as XAML.
Its key is where it is, which is what tells the host that a line it
already has is the same line: typing changes the line the point is on
and leaves every other one alone."
  (let* ((scale (float urusi-scale))
         (height (/ (plist-get line :height) scale))
         (spacing (/ (or (plist-get line :line-spacing) 0) scale))
         (above (/ (or (plist-get line :line-spacing-above) 0) scale)))
    `(Canvas :key ,(format "%s-%s" (plist-get line :kind) (plist-get line :y))
             :Canvas.Top ,(/ (plist-get line :y) scale)
             :Height ,height
             ,@(mapcar (lambda (run)
                         (funcall urusi-screen-run-function run height spacing above))
                       (plist-get line :runs)))))

(defvar urusi-screen--composing ""
  "What the input method is turning over, or an empty string.
It is not in any buffer: the input method has not settled on it, and
Emacs will not see it until it does.  Drawing it is this file's to do,
because the window that would otherwise draw it cannot be seen.")

(defvar urusi-screen--caret nil
  "Where the cursor last was, as (X Y WIDTH HEIGHT), or nil.
The host is told, so that the candidates of the input method appear
beside the text rather than in a corner.")

(defun urusi-screen--tell-caret (x y width height)
  "Tell the host the cursor is at X, Y and is WIDTH by HEIGHT."
  (let ((caret (list x y width height)))
    (unless (equal caret urusi-screen--caret)
      (setq urusi-screen--caret caret)
      (urusi--send (list :type "caret" :x x :y y :width width :height height)))))

(defun urusi-screen-cursor (window)
  "Return the cursor of WINDOW, to be laid over its text.
It is laid over rather than put in a line, so that moving it leaves
every line as it was and a keystroke costs one line of the screen.

It is not drawn while Emacs has it hidden, which is half the time while
it blinks; what is being composed is drawn all the same."
  (when-let* (((eq window (selected-window)))
              (cursor (window-screen-cursor window))
              (frame (window-frame window))
              (color (or (urusi-screen-color (face-attribute 'cursor :background frame t))
                         (urusi-screen-color (face-attribute 'default :foreground frame t)))))
    (let* ((scale (float urusi-scale))
           (left (/ (plist-get cursor :x) scale))
           (top (/ (plist-get cursor :y) scale))
           (height (/ (plist-get cursor :height) scale)))
      (let ((origin (urusi-screen--window-origin window)))
        (urusi-screen--tell-caret (+ (car origin) (plist-get cursor :x))
                                  (+ (cdr origin) (plist-get cursor :y))
                                  (plist-get cursor :width)
                                  (plist-get cursor :height)))
      `(Canvas :key "cursor"
               :IsHitTestVisible "False"
               (Rectangle :Canvas.Left ,left
                          :Canvas.Top ,top
                          :Width ,(/ 2 scale)
                          :Height ,height
                          :Fill ,color
                          ,@(unless (internal-show-cursor-p window)
                              '(:Visibility "Collapsed")))
               ,@(unless (string-empty-p urusi-screen--composing)
                   ;; What is being composed goes where it will end up,
                   ;; underlined, as an input method draws it anywhere
                   ;; else.
                   `((Border :Canvas.Left ,left
                             :Canvas.Top ,top
                             :Background ,(urusi-screen-color
                                           (face-attribute 'default :background frame t))
                             (TextBlock :Text ,urusi-screen--composing
                                        :FontFamily ,(urusi-screen-font-family)
                                        :FontSize ,(urusi-screen-font-size)
                                        :Foreground ,color
                                        :TextDecorations "Underline"))))))))

;;;; The windows

(defvar urusi-screen--built (make-hash-table :test #'equal)
  "What each line of this screen was built into, keyed by the line.")

(defvar urusi-screen--built-before (make-hash-table :test #'equal)
  "The same, for the screen before, which is what this one reuses.")

(defun urusi-screen--line (line)
  "Return LINE built, reusing what it was built into last time.
A line Emacs drew the same way is the same line, and building it again
would only arrive at what is already here.  Handing back the very
object from last time is also what lets `urusi-render' know, without
looking, that there is nothing to send for it."
  (puthash line
           (or (gethash line urusi-screen--built-before)
               (funcall urusi-screen-line-function line))
           urusi-screen--built))

(defun urusi-screen--start-screen ()
  "Begin a screen, keeping only what the one before it built.
Two screens' worth is all that is ever reused, and holding more would
be holding every line the session has ever shown."
  (setq urusi-screen--built-before urusi-screen--built)
  (setq urusi-screen--built (make-hash-table :test #'equal)))

(defvar urusi-screen--said-nothing 0
  "How many times there has been no screen to read, and it was said.")

(defun urusi-screen--nothing-to-draw (window)
  "Say that WINDOW has no screen to read, and why it might not.
A window with nothing in it looks the same as one this cannot read, and
the difference is not something to find out twice."
  (when (< urusi-screen--said-nothing 5)
    (cl-incf urusi-screen--said-nothing)
    (urusi--log "no rows for %S: frame visible %S, size %Sx%S, cursor %S"
                window
                (frame-visible-p (window-frame window))
                (frame-pixel-width (window-frame window))
                (frame-pixel-height (window-frame window))
                (window-screen-cursor window))))

(defun urusi-screen-window (window index)
  "Return WINDOW as XAML, where it sits on the frame.
INDEX says which window this is, and names the parts of it the host
keeps between one screen and the next.

The lines and the cursor are each a set of rows of their own, so that a
line that has not changed is not sent again and the cursor can move
without any line being touched.

What is drawn is cut to the window, as Emacs cuts it: a line can be
taller than the window has room for, the echo area's when it shows
text in a taller font for one, and Emacs shows as much of it as fits."
  (let* ((scale (float urusi-scale))
         (rows (window-screen-rows window))
         ;; Its edges are inside the frame's border, where
         ;; `window-pixel-left' and `window-pixel-top' do not count it.
         (edges (window-pixel-edges window))
         (width (/ (window-pixel-width window) scale))
         (height (/ (window-pixel-height window) scale))
         (background (urusi-screen-color
                      (face-attribute 'default :background (window-frame window) t))))
    (unless rows
      (urusi-screen--nothing-to-draw window))
    `(Canvas :Canvas.Left ,(/ (nth 0 edges) scale)
             :Canvas.Top ,(/ (nth 1 edges) scale)
             :Width ,width
             :Height ,height
             ,@(when background `(:Background ,background))
             (Canvas.Clip
              (RectangleGeometry :Rect ,(format "0,0,%s,%s" width height)))
             (Rows :key ,(format "window-%d" index)
                   :panel "Canvas"
                   ,@(mapcar #'urusi-screen--line rows))
             (Rows :key ,(format "cursor-%d" index)
                   :panel "Canvas"
                   ,@(when-let* ((cursor (funcall urusi-screen-cursor-function
                                                  window)))
                       (list cursor))))))

(defvar urusi-screen--windows-drawn 0
  "How many windows this screen has drawn so far.
It is what numbers them, so that no two share a name however many
frames they are spread over.")

(defun urusi-screen--frame-origin (frame)
  "Return where FRAME is, as (X . Y) pixels from its root frame's corner."
  (let ((x 0) (y 0))
    (while (frame-parent frame)
      (let ((position (frame-position frame)))
        (setq x (+ x (car position))
              y (+ y (cdr position))
              frame (frame-parent frame))))
    (cons x y)))

(defun urusi-screen--window-origin (window)
  "Return where WINDOW is, as (X . Y) pixels from its root frame's corner.
It is where its edges are, which are inside the frame's border:
`window-pixel-left' and `window-pixel-top' count from inside it."
  (let ((frame (urusi-screen--frame-origin (window-frame window)))
        (edges (window-pixel-edges window)))
    (cons (+ (car frame) (nth 0 edges))
          (+ (cdr frame) (nth 1 edges)))))

(defun urusi-screen--child-frames (frame)
  "Return the child frames of FRAME that can be seen."
  (cl-remove-if-not (lambda (child)
                      (and (eq (frame-parent child) frame)
                           (eq (frame-visible-p child) t)))
                    (frame-list)))

(defun urusi-screen--border-color (frame)
  "Return the colour of the border around FRAME, or nil for none.
A child frame's is the face `child-frame-border', where there is one,
and any frame's otherwise is the face `internal-border'."
  (cl-loop for face in '(child-frame-border internal-border)
           thereis (and (facep face)
                        (urusi-screen-color
                         (face-attribute face :background frame t)))))

(defun urusi-screen--frame-name (frame)
  "Return a name for FRAME that no other frame has while it lives."
  (format "%x" (sxhash-eq frame)))

(defcustom urusi-screen-child-frame-function #'urusi-screen-child-frame-body
  "Function that draws one child frame, such as a floating minibuffer.
It takes the frame and returns it as XAML, as big as the frame is;
where it goes on its parent is Emacs's to decide, and it is put there.
The one that draws it as Emacs would is `urusi-screen-child-frame-body',
which one of your own can wrap in whatever it likes: a shadow under
it, rounded corners, only for some frames and not others."
  :type 'function)

(cl-defun urusi-screen-child-frame-body (frame &key (border t) corner-radius)
  "Return FRAME, a child frame, as XAML, the way Emacs draws one.
That is its own background, the border around it, and its windows, with
its own children over those.

BORDER nil leaves the border out, for a frame that has something else
to set it off, a shadow for one; the room Emacs keeps for it is then
the frame's background.  CORNER-RADIUS rounds the corners of the frame,
the border with them, and cuts off what is drawn in them."
  (let* ((scale (float urusi-scale))
         (width (/ (frame-native-width frame) scale))
         (height (/ (frame-native-height frame) scale))
         (thickness (frame-internal-border-width frame))
         (border-color (and border (urusi-screen--border-color frame)))
         (background (urusi-screen-color
                      (face-attribute 'default :background frame t))))
    ;; The border is laid over the windows rather than drawn by the Border
    ;; around them, which would move them in by its thickness: where Emacs
    ;; put a window counts from the corner of the frame, border and all.
    `(Border :Width ,width
             :Height ,height
             ,@(when corner-radius `(:CornerRadius ,corner-radius))
             ,@(when background `(:Background ,background))
             (Grid
              ,(urusi-screen-windows frame)
              ,@(when (and border-color (< 0 thickness))
                  `((Border :BorderThickness ,(/ thickness scale)
                            :BorderBrush ,border-color
                            ,@(when corner-radius `(:CornerRadius ,corner-radius))
                            :IsHitTestVisible nil)))))))

(defun urusi-screen-child-frame (frame)
  "Return FRAME, a child frame, as a row of XAML, where it sits on its parent.
What is drawn is up to `urusi-screen-child-frame-function'.

It is a row of its own, and its windows are rows inside it, so that a
child frame that comes, goes or changes size is sent by itself: the
frame it is over stays as it was, and so do its own lines when only
where it is has changed."
  (let ((scale (float urusi-scale))
        (position (frame-position frame)))
    `(Canvas :key ,(urusi-screen--frame-name frame)
             :Canvas.Left ,(/ (car position) scale)
             :Canvas.Top ,(/ (cdr position) scale)
             ,(funcall urusi-screen-child-frame-function frame))))

(defun urusi-screen-windows (frame)
  "Return every window of FRAME, each where it is.
The echo area is among them: it is the minibuffer window, and Emacs
draws what it has to say there like anything else.

The child frames of FRAME that can be seen are drawn over its windows,
where Emacs put them: completion that pops up by the point, or a
minibuffer that floats in the middle of the frame."
  `(Canvas ,@(cl-loop for window in (window-list frame t)
                      collect (funcall urusi-screen-window-function
                                       window
                                       (1- (cl-incf urusi-screen--windows-drawn))))
           ;; There whether or not there are any, so that one coming or
           ;; going changes what is in it and nothing around it.
           (Rows :key ,(concat "frames-" (urusi-screen--frame-name frame))
                 :panel "Canvas"
                 ,@(mapcar #'urusi-screen-child-frame
                           (urusi-screen--child-frames frame)))))

;;;; The screen

(defcustom urusi-screen-components '(urusi-screen-windows)
  "Functions that build the screen.
Each takes the frame being shown and returns a tree for `urusi-render',
or nil for nothing at all.  They are passed in this order to
`urusi-screen-layout-function'.

The one whose symbol has a non-nil `urusi-screen-frame' property is
where the Emacs frame goes; see `urusi-screen-layout'.

A component can draw the window's title bar.  An element named
urusi-titlebar is what tells the host to take the window's own title
bar away and to let that element move the window, as a title bar does;
the controls on it are left to be clicked.  Without one, the window
keeps the title bar Windows gives it."
  :type '(repeat function))

(put 'urusi-screen-windows 'urusi-screen-frame t)

(defcustom urusi-screen-layout-function #'urusi-screen-layout
  "Function that puts the built components together into one tree.
It takes the list of what the components returned, in order, and the
frame they were built for."
  :type 'function)

(defun urusi-screen--frame-part-p (part)
  "Return non-nil if PART is where the Emacs frame goes."
  (get (car part) 'urusi-screen-frame))

(defun urusi-screen--in-row (tree row)
  "Return TREE placed in ROW of the grid around it."
  (cons (car tree) (append (list :Grid.Row row) (cdr tree))))

(defun urusi-screen-layout (parts frame)
  "Return PARTS from top to bottom, on the background of `default'.
PARTS is an alist of the component that built each one and what it
built.

The part where the Emacs frame goes takes whatever room the others
leave, and is put in an element named urusi-frame: the host makes the
frame as big as that element, so what else is on the screen is room
the frame does not have, and Emacs lays its text out to fit.  The other
parts take the room they need."
  (let ((background (urusi-screen-color (face-attribute 'default :background frame t))))
    `(Grid ,@(when background `(:Background ,background))
           (Grid.RowDefinitions
            ,@(mapcar (lambda (part)
                        `(RowDefinition :Height ,(if (urusi-screen--frame-part-p part)
                                                     "*"
                                                   "Auto")))
                      parts))
           ,@(cl-loop for part in parts
                      for row from 0
                      collect (if (urusi-screen--frame-part-p part)
                                  `(Grid :Name "urusi-frame" :Grid.Row ,row
                                         ,(cdr part))
                                (urusi-screen--in-row (cdr part) row))))))

(defun urusi-screen-tree (&optional frame)
  "Return the whole screen of FRAME as a tree for `urusi-render'."
  (urusi-screen--start-screen)
  (setq urusi-screen--windows-drawn 0)
  (let* ((frame (or frame (urusi-root-frame)))
         (parts (delq nil
                      (mapcar (lambda (component)
                                (when-let* ((tree (funcall component frame)))
                                  (cons component tree)))
                              urusi-screen-components))))
    (funcall urusi-screen-layout-function parts frame)))

(defvar urusi-screen--timed 0
  "How many screens have been timed, of `urusi-screen-timings'.")

(defcustom urusi-screen-timings 30
  "How many of the first screens to say how long they took.
Long enough to see what a keystroke costs, and then quiet."
  :type 'integer)

(defun urusi-screen-render ()
  "Show the screen in the host window."
  (interactive)
  (urusi-screen--fit-frame (urusi-root-frame))
  (if (<= urusi-screen-timings urusi-screen--timed)
      (progn (redisplay)
             (urusi-render (urusi-screen-tree)))
    (cl-incf urusi-screen--timed)
    (let* ((start (current-time))
           ;; What is drawn is read out of the screen Emacs drew, so
           ;; there has to be one, and it has to be of what the buffers
           ;; hold now.
           (_ (redisplay))
           (drawn (current-time))
           (tree (urusi-screen-tree))
           (built (current-time))
           (sent (urusi-render tree)))
      (urusi--log "screen %d: redisplay %.1fms, build %.1fms, send %.1fms, %s"
                  urusi-screen--timed
                  (* 1000 (float-time (time-subtract drawn start)))
                  (* 1000 (float-time (time-subtract built drawn)))
                  (* 1000 (float-time (time-since built)))
                  sent))))

(defvar urusi-screen--pending nil
  "Timer that will show the screen, when one is waiting to run.")

(defun urusi-screen--cursor-shown (&rest _)
  "Draw again now that the cursor has been hidden or shown.
This is how the cursor blinks: a timer hides it and shows it again, with
no command between, so nothing else would draw it."
  (urusi-screen--after-command))

(defun urusi-screen--after-command ()
  "Show the screen once what is happening now has finished happening."
  (unless urusi-screen--pending
    (setq urusi-screen--pending
          (urusi-when-idle
           (lambda ()
             (setq urusi-screen--pending nil)
             (condition-case err
                 (urusi-screen-render)
               (error (urusi--log "screen: %S" err))))))))

;;;###autoload
(define-minor-mode urusi-screen-mode
  "Show the Emacs screen in the host window, as XAML."
  :global t
  (if urusi-screen-mode
      (progn
        (add-hook 'post-command-hook #'urusi-screen--after-command)
        (add-hook 'urusi-after-event-hook #'urusi-screen--after-command)
        (advice-add 'internal-show-cursor :after #'urusi-screen--cursor-shown)
        (add-hook 'urusi-stale-hook #'urusi-screen-render)
        (add-hook 'urusi-message-hook #'urusi-screen--message)
        (add-hook 'urusi-host-event-functions #'urusi-screen--window-changed)
        (urusi-forget)
        ;; Nothing has been displayed yet when this runs during startup,
        ;; so Emacs has no screen to tell about; ask again once it has.
        (urusi-screen--after-command)
        (urusi--log "screen mode on"))
    (remove-hook 'post-command-hook #'urusi-screen--after-command)
    (remove-hook 'urusi-after-event-hook #'urusi-screen--after-command)
    (advice-remove 'internal-show-cursor #'urusi-screen--cursor-shown)
    (remove-hook 'urusi-stale-hook #'urusi-screen-render)
    (remove-hook 'urusi-message-hook #'urusi-screen--message)
    (remove-hook 'urusi-host-event-functions #'urusi-screen--window-changed)))

(defvar urusi-screen--room nil
  "How much room the host has for the frame, as (WIDTH . HEIGHT) in pixels.")

(defun urusi-screen--fit-frame (frame)
  "Make FRAME as big as the room the host has for it, if it is not.
Return non-nil if it had to be resized.

The room is for the whole frame, fringes and all, but `set-frame-size'
sizes only the part of it text goes in: what is around that is taken
off first.  What is around it can change afterwards, as when the
fringes are made wider, and Emacs then keeps the text as big as it was
and makes the frame bigger, past the room it has; which is why this is
asked again every time the screen is drawn."
  (when-let* ((room urusi-screen--room)
              ((not (and (= (car room) (frame-native-width frame))
                         (= (cdr room) (frame-native-height frame))))))
    (set-frame-size frame
                    (- (car room) (- (frame-native-width frame) (frame-text-width frame)))
                    (- (cdr room) (- (frame-native-height frame) (frame-text-height frame)))
                    t)
    t))

(defun urusi-screen--resize (message)
  "Lay the frame out to the size the host says it has room for.

The frame's own window is on no screen and its size means nothing to
Windows, so the size comes as a message rather than as a window being
resized.  Which also means it cannot come back: nothing here moves a
window that something else would then tell us about."
  (let ((width (plist-get message :width))
        (height (plist-get message :height)))
    (when (and (numberp width) (numberp height) (< 0 width) (< 0 height))
      (setq urusi-screen--room (cons (truncate width) (truncate height)))
      (urusi-screen--fit-frame (urusi-root-frame))
      (urusi-forget)
      (urusi-screen-render))))

(defun urusi-screen--window-changed (event _message)
  "Draw again when EVENT changes the window in a way the screen shows.
A maximize button that restores once the window is maximized, or colours
chosen for a light window when Windows has turned dark."
  (when (memq event '(state theme))
    (urusi-screen--after-command)))

(defun urusi-screen--message (message)
  "Answer MESSAGE from the host, if it is this file's to answer."
  (pcase (plist-get message :type)
    ("measured" (urusi-screen--measured message))
    ("resize" (urusi-screen--resize message))
    ("composition"
     (setq urusi-screen--composing (or (plist-get message :text) ""))
     (urusi-screen-render))))

(provide 'urusi-screen)
;;; urusi-screen.el ends here
