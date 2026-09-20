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
;; and `-cursor-function' for the cursor.

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

(defun urusi-screen-color (color)
  "Return COLOR as the #rrggbb XAML wants, or nil if there is no such color."
  (when-let* ((values (and (stringp (urusi-screen--set color))
                           (color-values color))))
    (apply #'format "#%02x%02x%02x" (mapcar (lambda (v) (/ v 256)) values))))

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
It takes the run, as `window-screen-rows' gives it, and how tall the
line is in the pixels XAML counts in."
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

(defun urusi-screen-run (run height)
  "Return RUN, one stretch of a line, as XAML, HEIGHT pixels tall.

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
    `(Border :Canvas.Left ,(/ (plist-get run :x) scale)
             :Width ,width
             :Height ,height
             ,@(when background `(:Background ,background))
             ,@(when text
                 (list (urusi-screen--text run text family size
                                           (/ width (length text)) height))))))

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
         (height (/ (plist-get line :height) scale)))
    `(Canvas :key ,(format "%s-%s" (plist-get line :kind) (plist-get line :y))
             :Canvas.Top ,(/ (plist-get line :y) scale)
             :Height ,height
             ,@(mapcar (lambda (run) (funcall urusi-screen-run-function run height))
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
every line as it was and a keystroke costs one line of the screen."
  (when-let* (((eq window (selected-window)))
              (cursor (window-screen-cursor window))
              (color (or (urusi-screen-color (face-attribute 'cursor :background))
                         (urusi-screen-color (face-attribute 'default :foreground)))))
    (let* ((scale (float urusi-scale))
           (left (/ (plist-get cursor :x) scale))
           (top (/ (plist-get cursor :y) scale))
           (height (/ (plist-get cursor :height) scale)))
      (urusi-screen--tell-caret (plist-get cursor :x) (plist-get cursor :y)
                                (plist-get cursor :width)
                                (plist-get cursor :height))
      `(Canvas :key "cursor"
               :IsHitTestVisible "False"
               (Rectangle :Canvas.Left ,left
                          :Canvas.Top ,top
                          :Width ,(/ 2 scale)
                          :Height ,height
                          :Fill ,color)
               ,@(unless (string-empty-p urusi-screen--composing)
                   ;; What is being composed goes where it will end up,
                   ;; underlined, as an input method draws it anywhere
                   ;; else.
                   `((Border :Canvas.Left ,left
                             :Canvas.Top ,top
                             :Background ,(urusi-screen-color
                                           (face-attribute 'default :background))
                             (TextBlock :Text ,urusi-screen--composing
                                        :FontFamily ,(urusi-screen-font-family)
                                        :FontSize ,(urusi-screen-font-size)
                                        :Foreground ,color
                                        :TextDecorations "Underline"))))))))

;;;; The windows

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
without any line being touched."
  (let* ((scale (float urusi-scale))
         (rows (window-screen-rows window))
         (background (urusi-screen-color
                      (face-attribute 'default :background nil t))))
    (unless rows
      (urusi-screen--nothing-to-draw window))
    `(Canvas :Canvas.Left ,(/ (window-pixel-left window) scale)
             :Canvas.Top ,(/ (window-pixel-top window) scale)
             :Width ,(/ (window-pixel-width window) scale)
             :Height ,(/ (window-pixel-height window) scale)
             ,@(when background `(:Background ,background))
             (Rows :key ,(format "window-%d" index)
                   :panel "Canvas"
                   ,@(mapcar (lambda (line)
                               (funcall urusi-screen-line-function line))
                             rows))
             (Rows :key ,(format "cursor-%d" index)
                   :panel "Canvas"
                   ,@(when-let* ((cursor (funcall urusi-screen-cursor-function
                                                  window)))
                       (list cursor))))))

(defun urusi-screen-windows (frame)
  "Return every window of FRAME, each where it is.
The echo area is among them: it is the minibuffer window, and Emacs
draws what it has to say there like anything else."
  `(Canvas ,@(cl-loop for window in (window-list frame t)
                      for index from 0
                      collect (funcall urusi-screen-window-function
                                       window index))))

;;;; The screen

(defcustom urusi-screen-components '(urusi-screen-windows)
  "Functions that build the screen.
Each takes the frame being shown and returns a tree for `urusi-render',
or nil for nothing at all.  They are passed in this order to
`urusi-screen-layout-function'."
  :type '(repeat function))

(defcustom urusi-screen-layout-function #'urusi-screen-layout
  "Function that puts the built components together into one tree.
It takes the list of what the components returned, in order, and the
frame they were built for."
  :type 'function)

(defun urusi-screen-layout (parts _frame)
  "Return PARTS, one over another, on the background of `default'.
PARTS is an alist of the component that built each one and what it
built.  The windows place themselves, so what holds them has nothing to
decide; a layout that wants to move them can say so here instead."
  (let ((background (urusi-screen-color (face-attribute 'default :background))))
    `(Grid ,@(when background `(:Background ,background))
           ,@(mapcar #'cdr parts))))

(defun urusi-screen-tree (&optional frame)
  "Return the whole screen of FRAME as a tree for `urusi-render'."
  (let* ((frame (or frame (selected-frame)))
         (parts (delq nil
                      (mapcar (lambda (component)
                                (when-let* ((tree (funcall component frame)))
                                  (cons component tree)))
                              urusi-screen-components))))
    (funcall urusi-screen-layout-function parts frame)))

(defun urusi-screen-render ()
  "Show the screen in the host window."
  (interactive)
  ;; What is drawn is read out of the screen Emacs drew, so there has to
  ;; be one, and it has to be of what the buffers hold now.
  (redisplay)
  (urusi-render (urusi-screen-tree)))

(defvar urusi-screen--pending nil
  "Timer that will show the screen, when one is waiting to run.")

(defun urusi-screen--after-command ()
  "Show the screen once what is happening now has finished happening."
  (unless urusi-screen--pending
    (setq urusi-screen--pending
          (run-with-idle-timer
           0 nil (lambda ()
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
        (add-hook 'urusi-stale-hook #'urusi-screen-render)
        (add-hook 'urusi-message-hook #'urusi-screen--message)
        (urusi-forget)
        ;; Nothing has been displayed yet when this runs during startup,
        ;; so Emacs has no screen to tell about; ask again once it has.
        (urusi-screen--after-command)
        (urusi--log "screen mode on"))
    (remove-hook 'post-command-hook #'urusi-screen--after-command)
    (remove-hook 'urusi-stale-hook #'urusi-screen-render)
    (remove-hook 'urusi-message-hook #'urusi-screen--message)))

(defun urusi-screen--message (message)
  "Answer MESSAGE from the host, if it is this file's to answer."
  (pcase (plist-get message :type)
    ("measured" (urusi-screen--measured message))
    ("composition"
     (setq urusi-screen--composing (or (plist-get message :text) ""))
     (urusi-screen-render))))

(provide 'urusi-screen)
;;; urusi-screen.el ends here
