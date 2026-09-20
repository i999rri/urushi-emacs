;;; urusi-screen.el --- The Emacs screen, built as XAML  -*- lexical-binding: t; -*-

;;; Commentary:

;; Builds what Emacs shows as a tree of XAML elements and sends it to the
;; host to draw.  Emacs decides the content: which lines are on screen,
;; where they are broken, what face each stretch of text carries.  What
;; that looks like is decided here, in Lisp, and so is yours to change.
;;
;; What comes with it is a default, not the shape of the thing.  The
;; screen is a list of components, each a function of the window
;; returning a tree for `urusi-render':
;;
;;   `urusi-screen-header'   the tab line and header line
;;   `urusi-screen-buffer'   the text of the window, a line per element
;;   `urusi-screen-status'   the mode line
;;   `urusi-screen-echo'     the echo area, or the minibuffer in use
;;
;; Take one out of `urusi-screen-components' and it is gone.  Write your
;; own and it is there.  Wrap one and it keeps its contents:
;;
;;   (defun my-rounded-buffer (window)
;;     `(Border :CornerRadius 8 :Padding 12 :Background "#1b1b1f"
;;              ,(urusi-screen-buffer window)))
;;   (setq urusi-screen-components
;;         '(my-rounded-buffer urusi-screen-status))
;;
;; How the components are stacked is `urusi-screen-layout-function',
;; which is a function of the built parts, so a side-by-side screen or
;; one part floating over another is a matter of returning that tree.
;;
;; To build components of your own, the pieces the ones here are made of
;; are `urusi-screen-runs' (text to inlines, split on face),
;; `urusi-screen-line' (inlines to an element), `urusi-screen-color' and
;; `urusi-screen-face-attribute'.

;;; Code:

(require 'cl-lib)
(require 'subr-x)
(require 'urusi)

(defgroup urusi-screen nil
  "The Emacs screen, built as XAML."
  :group 'urusi)

(defcustom urusi-screen-font "Consolas"
  "Font to show the screen in, when Emacs has no font of its own to name.
That is so on a terminal, where Emacs never asked for one."
  :type 'string)

(defvar urusi-screen--advance nil
  "How wide the host draws a character, as (NARROW . WIDE), or nil.
Emacs lays its text out on a grid of whole columns -- one for most
characters, two for the likes of kana -- while the host draws at the
width the font asks for, so the two drift apart across a line.  Asking
the host how wide it draws one of each is what tells Emacs how much to
correct for; see `urusi-screen--spacing'.")

(defun urusi-screen--measure ()
  "Ask the host how wide it draws a character of the screen font."
  (setq urusi-screen--advance nil)
  (urusi--send (list :type "measure"
                     :family (urusi-screen-font-family)
                     :size (urusi-screen-font-size))))

(defun urusi-screen--measured (message)
  "Take the widths the host reported in MESSAGE, and draw again with them."
  (setq urusi-screen--advance (cons (plist-get message :narrow)
                                    (plist-get message :wide)))
  (urusi--log "font %s %s, cell %s, drawn %s/%s, spacing %s/%s"
              (urusi-screen-font-family) (urusi-screen-font-size)
              (default-font-width)
              (car urusi-screen--advance) (cdr urusi-screen--advance)
              (urusi-screen--spacing 1) (urusi-screen--spacing 2))
  (urusi-forget)
  (urusi-screen-render))

(defun urusi-screen-columns (character)
  "Return how many columns Emacs gives CHARACTER, as far as this file cares.
What is not twice as wide is treated as once, tabs included: a tab is
drawn as the one character it is until there is something better."
  (if (= (char-width character) 2) 2 1))

(defun urusi-screen--spacing (columns)
  "Return what to add to each character that Emacs gives COLUMNS columns.
That is what makes a line as wide here as Emacs laid it out.  XAML
counts it in thousandths of the font size."
  (let ((drawn (if (= columns 1)
                   (car-safe urusi-screen--advance)
                 (cdr-safe urusi-screen--advance))))
    (if (and (numberp drawn) (< 0 drawn))
        (round (* 1000 (/ (- (/ (* columns (default-font-width))
                                (float urusi-scale))
                             drawn)
                          (urusi-screen-font-size))))
      0)))

(defun urusi-screen-line-height ()
  "Return the height of a screen line, in the pixels XAML counts in.
It is the height Emacs laid the text out with, so that a screenful
here is a screenful there."
  (/ (default-line-height) (float urusi-scale)))

(defcustom urusi-screen-components
  '(urusi-screen-header
    urusi-screen-buffer
    urusi-screen-status
    urusi-screen-echo)
  "Functions that build the screen.
Each takes the window being shown and returns a tree for
`urusi-render', or nil for nothing at all.  They are passed in this
order to `urusi-screen-layout-function'."
  :type '(repeat function))

(defcustom urusi-screen-layout-function #'urusi-screen-layout
  "Function that puts the built components together into one tree.
It takes the list of what the components returned, in order, and the
window they were built for."
  :type 'function)

;;;; Faces

(defun urusi-screen-face-attribute (face attribute)
  "Return ATTRIBUTE of FACE, or nil if FACE says nothing about it.
FACE is what a text property holds: a face name, a list of face names,
or a plist of attributes."
  (let ((value
         (cond
          ((null face) nil)
          ((keywordp (car-safe face)) (plist-get face attribute))
          ((consp face)
           (cl-some (lambda (one) (urusi-screen-face-attribute one attribute))
                    face))
          ((symbolp face)
           (and (facep face) (face-attribute face attribute nil t)))
          (t nil))))
    (unless (eq value 'unspecified) value)))

(defun urusi-screen-color (color)
  "Return COLOR as the #rrggbb XAML wants, or nil if there is no such color."
  (when-let* ((values (and color (stringp color) (color-values color))))
    (apply #'format "#%02x%02x%02x" (mapcar (lambda (v) (/ v 256)) values))))

(defun urusi-screen-font-family ()
  "Return the family of the default font, as a name XAML knows."
  (let ((family (face-attribute 'default :family)))
    (if (and (stringp family) (not (equal family "default")))
        family
      urusi-screen-font)))

(defun urusi-screen-font-size ()
  "Return the size of the default font, in the pixels XAML counts in."
  (let ((height (face-attribute 'default :height)))
    ;; A height is tenths of a point on a frame that has a font, and a
    ;; multiplier on one that has none.  XAML counts 96ths of an inch.
    (if (and (integerp height) (<= 10 height))
        (/ (* height 96.0) 720.0)
      14.0)))

(defun urusi-screen-cursor (window)
  "Return the cursor of WINDOW, to be laid over the text.
Emacs knows where the point is on the screen, to the pixel, because it
is Emacs that put it there; ask, and draw a bar at that spot.  Nothing
about the lines has to change for the cursor to move, which is what
keeps a keystroke to one line of the screen."
  (when-let* ((point (if (eq window (selected-window))
                         (point)
                       (window-point window)))
              ;; Emacs answers this from the screen it last drew, and
              ;; has drawn none at all when this runs as it starts.
              (position (or (posn-at-point point window)
                            (progn (redisplay) (posn-at-point point window))))
              (xy (posn-x-y position))
              (color (or (urusi-screen-color (face-attribute 'cursor :background))
                         (urusi-screen-color (face-attribute 'default :foreground)))))
    ;; Emacs counts in the pixels of the screen, XAML in 96ths of an inch.
    (let ((scale (float urusi-scale)))
      `(Canvas :key "cursor"
               :IsHitTestVisible "False"
               (Rectangle :Canvas.Left ,(/ (car xy) scale)
                          :Canvas.Top ,(/ (cdr xy) scale)
                          :Width ,(/ 2 scale)
                          :Height ,(urusi-screen-line-height)
                          :Fill ,color)))))

;;;; Text

(defun urusi-screen-run (text face &optional columns)
  "Return TEXT in FACE as a XAML inline.
COLUMNS is how many columns Emacs gives each character of TEXT, which
is what decides how far apart they are drawn."
  (let ((foreground (urusi-screen-color
                     (urusi-screen-face-attribute face :foreground)))
        (weight (urusi-screen-face-attribute face :weight))
        (slant (urusi-screen-face-attribute face :slant)))
    `(Run :Text ,text
          :CharacterSpacing ,(urusi-screen--spacing (or columns 1))
          ,@(when foreground `(:Foreground ,foreground))
          ,@(when (memq weight '(bold semi-bold ultra-bold extra-bold))
              '(:FontWeight "Bold"))
          ,@(when (memq slant '(italic oblique)) '(:FontStyle "Italic"))
          ,@(when (urusi-screen-face-attribute face :underline)
              '(:TextDecorations "Underline")))))

(defun urusi-screen-runs (start end &optional object)
  "Return the text from START to END as XAML inlines.
An inline ends where the face changes, and where the width of the
characters changes: how far apart they are drawn belongs to the inline
they are in, and Emacs draws a character of two columns twice as far
from the next as one of one.

OBJECT is a string to read from, or nil for the current buffer, in
which case overlays count as well."
  (cl-flet ((character (at) (if object (aref object at) (char-after at)))
            (face-at (at) (if object
                              (get-text-property at 'face object)
                            (get-char-property at 'face))))
    (let ((runs nil)
          (position start))
      (while (< position end)
        (let* ((face (face-at position))
               (columns (urusi-screen-columns (character position)))
               (face-end (if object
                             (or (next-single-property-change position 'face object end)
                                 end)
                           (next-single-char-property-change position 'face nil end)))
               (next (1+ position)))
          (while (and (< next face-end)
                      (= columns (urusi-screen-columns (character next))))
            (setq next (1+ next)))
          (push (urusi-screen-run
                 (if object
                     (substring-no-properties object position next)
                   (buffer-substring-no-properties position next))
                 face columns)
                runs)
          (setq position next)))
      (nreverse runs))))

(defun urusi-screen-line (key runs &optional face)
  "Return a screen line showing RUNS, with the background of FACE.
KEY says which line this is between one screen and the next: a line
whose key and contents are unchanged is left alone by the host, and one
that has only moved is moved rather than built again.

An empty line still takes its height, which is what the space is for.

A TextBlock takes no background of its own and inherits no font from
what it is in, so a line that wants a background is wrapped in a
Border, and the font is on the line itself."
  (let* ((background (urusi-screen-color
                      (urusi-screen-face-attribute face :background)))
         (foreground (urusi-screen-color
                      (or (urusi-screen-face-attribute face :foreground)
                          (face-attribute 'default :foreground))))
         (text `(TextBlock :TextWrapping "NoWrap"
                           :FontFamily ,(urusi-screen-font-family)
                           :FontSize ,(urusi-screen-font-size)
                           :LineHeight ,(urusi-screen-line-height)
                           :LineStackingStrategy "BlockLineHeight"
                           ,@(when foreground `(:Foreground ,foreground))
                           ,@(or runs (list '(Run :Text " "))))))
    (if background
        `(Border :key ,key :Background ,background ,text)
      `(TextBlock :key ,key ,@(cdr text)))))

;;;; The components

(defun urusi-screen-header (window)
  "Return what sits above the buffer of WINDOW: the tab and header lines."
  (let ((lines (delq nil
                     (list (and tab-line-format
                                (format-mode-line tab-line-format nil window))
                           (and header-line-format
                                (format-mode-line header-line-format nil window))))))
    (when lines
      `(Rows :key "header"
             ,@(cl-loop for line in lines
                        for index from 0
                        collect (urusi-screen-line
                                 (format "header-%d" index)
                                 (urusi-screen-runs 0 (length line) line)
                                 'header-line))))))

(defun urusi-screen-buffer (window)
  "Return the text WINDOW shows, one element per screen line.
Where each screen line starts and ends is asked of Emacs, so that the
text is broken exactly where Emacs has it broken."
  (with-current-buffer (window-buffer window)
    (let ((lines nil)
          (end (window-end window t)))
      (save-excursion
        (goto-char (window-start window))
        (while (< (point) end)
          (let ((start (point)))
            (vertical-motion 1 window)
            (when (= (point) start)     ; nothing moved: this is the end
              (goto-char end))
            (let* ((stop (min (point) end))
                   ;; The newline ends the line; it is not something to show.
                   (last (if (and (< start stop) (eq (char-before stop) ?\n))
                             (1- stop)
                           stop)))
              ;; Where the line starts is what it is: a line that has
              ;; scrolled is the same line, and stays as it is.
              (push (urusi-screen-line (number-to-string start)
                                       (urusi-screen-runs start last))
                    lines))))
        ;; A buffer that ends in a newline shows one more line after it,
        ;; empty, and that is where the point usually is.
        (when (and (= (point) end) (< (point-min) end) (eq (char-before end) ?
))
          (push (urusi-screen-line (number-to-string end) nil) lines)))
      ;; The cursor is laid over the lines rather than put in one, so
      ;; that moving it leaves every line as it was.
      `(Grid (Rows :key "buffer" ,@(nreverse lines))
             (Rows :key "cursor"
                   ,@(when-let* ((cursor (urusi-screen-cursor window)))
                       (list cursor)))))))

(put 'urusi-screen-buffer 'urusi-screen-stretch t)

(defun urusi-screen-status (window)
  "Return the mode line of WINDOW."
  (when mode-line-format
    (let ((line (format-mode-line mode-line-format nil window)))
      `(Rows :key "status"
             ,(urusi-screen-line "mode-line"
                                 (urusi-screen-runs 0 (length line) line)
                                 'mode-line)))))

(defun urusi-screen-echo (_window)
  "Return the echo area, or the minibuffer while it is in use."
  (let ((text (cond
               ((minibufferp (window-buffer (minibuffer-window)))
                (with-current-buffer (window-buffer (minibuffer-window))
                  (buffer-substring (point-min) (point-max))))
               ((current-message))
               (t ""))))
    `(Rows :key "echo"
           ,(urusi-screen-line "echo" (urusi-screen-runs 0 (length text) text)))))

;;;; The screen

(defun urusi-screen-layout (parts _window)
  "Return PARTS laid out from top to bottom, on the background of `default'.
PARTS is an alist of the component that built each one and what it
built.  A component whose symbol has a non-nil `urusi-screen-stretch'
property takes the room the others do not want, which is what puts the
mode line at the foot of the window rather than under the last line of
the buffer.

A panel has no colour or font to give its contents; each line carries
its own."
  (let ((background (urusi-screen-color (face-attribute 'default :background))))
    `(Grid ,@(when background `(:Background ,background))
           (Grid.RowDefinitions
            ,@(mapcar (lambda (part)
                        `(RowDefinition
                          :Height ,(if (get (car part) 'urusi-screen-stretch)
                                       "*"
                                     "Auto")))
                      parts))
           ,@(cl-loop for part in parts
                      for row from 0
                      collect (urusi-screen--in-row (cdr part) row)))))

(defun urusi-screen--in-row (tree row)
  "Return TREE placed in ROW of the grid around it."
  (cons (car tree) (append (list :Grid.Row row) (cdr tree))))

(defun urusi-screen-tree (&optional window)
  "Return the whole screen of WINDOW as a tree for `urusi-render'."
  (let* ((window (or window (selected-window)))
         (parts (delq nil
                      (mapcar (lambda (component)
                                (when-let* ((tree (funcall component window)))
                                  (cons component tree)))
                              urusi-screen-components))))
    (funcall urusi-screen-layout-function parts window)))

(defun urusi-screen-render ()
  "Show the screen in the host window."
  (interactive)
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
        (urusi-screen--measure)
        ;; Nothing has been displayed yet when this runs during startup,
        ;; so Emacs has no screen to tell about; ask again once it has.
        (urusi-screen--after-command)
        (urusi--log "screen mode on"))
    (remove-hook 'post-command-hook #'urusi-screen--after-command)
    (remove-hook 'urusi-stale-hook #'urusi-screen-render)
    (remove-hook 'urusi-message-hook #'urusi-screen--message)))

(defun urusi-screen--message (message)
  "Take MESSAGE from the host, if it is one this file asked for."
  (when (equal (plist-get message :type) "measured")
    (urusi-screen--measured message)))

(provide 'urusi-screen)
;;; urusi-screen.el ends here
