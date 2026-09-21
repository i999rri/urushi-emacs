;;; urusi-statusbar.el --- A status bar drawn by the host  -*- lexical-binding: t; -*-

;;; Commentary:

;; One bar for the whole window, as most applications have, saying
;; what there is to say about the window being worked in: where the
;; point is, what the buffer is, whether anything is wrong with it.
;; Emacs says the same in the mode line of each window, and an init
;; file that has this bar may well do without those.
;;
;; The bar is two lists of segments, one from the left and one from the
;; right.  A segment is a function of the window, returning XAML or nil
;; for nothing, and any function will do; the ones here are the things
;; a mode line usually says.  Written as a list, (FUNCTION PROPERTIES...),
;; it is given PROPERTIES as well, which the ones here put on what they
;; draw:
;;
;;   (defun my-statusbar (frame)
;;     (urusi-statusbar frame
;;                      :left '(urusi-statusbar-vc urusi-statusbar-buffer)
;;                      :right '((urusi-statusbar-position :FontSize 12)
;;                               urusi-statusbar-major-mode)
;;                      :Height 24))
;;   (setq urusi-screen-components '(urusi-screen-windows my-statusbar))
;;
;; A segment of your own draws itself, or uses `urusi-statusbar-text' and
;; `urusi-statusbar-button', which look like the rest.

;;; Code:

(require 'cl-lib)
(require 'urusi)
(require 'urusi-screen)
(require 'urusi-titlebar)
(require 'warnings)

(declare-function flymake-diagnostics "flymake" (&optional beg end))
(declare-function flymake-diagnostic-type "flymake" (diag))
(declare-function flymake--severity "flymake" (type))
(declare-function flymake-show-buffer-diagnostics "flymake" ())

(defgroup urusi-statusbar nil
  "A status bar drawn by the host."
  :group 'urusi)

(defconst urusi-statusbar-hover "#28808080"
  "Colour laid over a segment that can be clicked, under the pointer.
Grey and mostly clear, so that it shows on a bar of any colour.")

(defun urusi-statusbar-window (&optional frame)
  "Return the window the bar is about, on FRAME's root frame.
That is the selected window, or while the minibuffer is being typed in,
the window that was selected before, which is what whatever is being
typed is about."
  (let ((window (selected-window)))
    (if (and (window-minibuffer-p window) (minibuffer-selected-window))
        (minibuffer-selected-window)
      (if (window-minibuffer-p window)
          (frame-selected-window (urusi-root-frame frame))
        window))))

;;;; What a segment looks like

(defun urusi-statusbar-text (text &rest properties)
  "Return TEXT as a segment of the bar.
PROPERTIES are more properties of the TextBlock it is in, and override
the ones it has."
  `(TextBlock :Text ,(urusi-literal text)
              ,@(urusi-titlebar--merge
                 properties
                 '(:VerticalAlignment "Center"
                   :Margin "8,0,8,0"
                   :TextTrimming "CharacterEllipsis"))))

(defun urusi-statusbar-button (text action &rest properties)
  "Return TEXT as a segment of the bar that does ACTION when clicked.
PROPERTIES are more properties of the Button, and override the ones it
has.  It does not take the focus, which would take the keys."
  `(Button :Content ,(urusi-literal text)
           :on-Click ,action
           ,@(urusi-titlebar--merge
              properties
              '(:VerticalAlignment "Stretch"
                :Padding "8,0,8,0"
                :CornerRadius 0
                :BorderThickness 0
                :Background "Transparent"
                :IsTabStop nil
                :AllowFocusOnInteraction nil))))

;;;; The segments a mode line usually has

(defun urusi-statusbar-buffer (window &rest properties)
  "The name of the buffer in WINDOW, with a dot when it is not saved.
PROPERTIES are those of `urusi-statusbar-text'."
  (let ((buffer (window-buffer window)))
    (apply #'urusi-statusbar-text
           (concat (buffer-name buffer)
                   (when (and (buffer-file-name buffer) (buffer-modified-p buffer))
                     " ●"))
           properties)))

(defun urusi-statusbar-position (window &rest properties)
  "Where the point of WINDOW is, as its line and column.
PROPERTIES are those of `urusi-statusbar-text'."
  (with-current-buffer (window-buffer window)
    (let ((point (window-point window)))
      (apply #'urusi-statusbar-text
             (format "Ln %d, Col %d"
                     (line-number-at-pos point)
                     (1+ (save-excursion (goto-char point) (current-column))))
             properties))))

(defun urusi-statusbar-major-mode (window &rest properties)
  "The name of the major mode of the buffer in WINDOW.
PROPERTIES are those of `urusi-statusbar-text'.
A mode of CC Mode puts how it is set after its name, \"C#//l\" for C#
with line comments and electric keys, which is not the language's
name: it is left out, as CC Mode itself finds the name."
  (let* ((buffer (window-buffer window))
         (name (string-trim (format-mode-line mode-name nil window buffer))))
    (apply
     #'urusi-statusbar-text
     (if (and (boundp 'c-buffer-is-cc-mode)
              (buffer-local-value 'c-buffer-is-cc-mode buffer)
              (string-match "\\`\\([^/]+\\)/" name))
         (match-string 1 name)
       name)
     properties)))

(defun urusi-statusbar-encoding (window &rest properties)
  "How the file in WINDOW is encoded, and how its lines end.
PROPERTIES are those of `urusi-statusbar-text'."
  (with-current-buffer (window-buffer window)
    (when buffer-file-name
      (let* ((system buffer-file-coding-system)
             (name (symbol-name (coding-system-base system)))
             (eol (pcase (coding-system-eol-type system)
                    (0 "LF") (1 "CRLF") (2 "CR"))))
        (apply #'urusi-statusbar-text
               (string-join (delq nil (list (upcase (string-remove-suffix
                                                     "-with-signature" name))
                                            eol))
                            "  ")
               properties)))))

(defun urusi-statusbar-vc (window &rest properties)
  "The branch the file in WINDOW is on, as version control says it.
PROPERTIES are those of `urusi-statusbar-text'."
  (when-let* ((vc (buffer-local-value 'vc-mode (window-buffer window)))
              (vc (string-trim (substring-no-properties vc)))
              ;; " Git-main", " Git:main": the branch after the backend.
              (branch (and (string-match "\\`[^-:@]+[-:@]\\(.*\\)\\'" vc)
                           (match-string 1 vc))))
    (apply #'urusi-statusbar-text (concat (string #x2387) " " branch) properties)))

(defun urusi-statusbar-message (_window &rest properties)
  "What Emacs is saying in the echo area, while it says it.
Only its first line, cut off where there is no more room when it is in
the FILL of `urusi-statusbar'.  PROPERTIES are those of
`urusi-statusbar-text'."
  (when-let* ((message (current-message))
              (line (car (split-string (substring-no-properties message) "\n")))
              ((not (string-empty-p line))))
    (apply #'urusi-statusbar-text line properties)))

(defun urusi-statusbar-diagnostics (window &rest properties)
  "How many errors and warnings Flymake has found in WINDOW's buffer.
Clicking it lists them.  PROPERTIES are those of `urusi-statusbar-button'."
  (with-current-buffer (window-buffer window)
    (when (bound-and-true-p flymake-mode)
      (let ((errors 0) (warnings 0))
        (dolist (diagnostic (flymake-diagnostics))
          (let ((severity (flymake--severity (flymake-diagnostic-type diagnostic))))
            (cond ((>= severity (warning-numeric-level :error)) (cl-incf errors))
                  ((>= severity (warning-numeric-level :warning)) (cl-incf warnings)))))
        (let ((buffer (current-buffer)))
          (apply #'urusi-statusbar-button
                 (format "%c %d  %c %d" #x2297 errors #x26A0 warnings)
                 (lambda ()
                   (with-current-buffer buffer
                     (flymake-show-buffer-diagnostics)))
                 properties))))))

;;;; The bar

(cl-defun urusi-statusbar (frame &rest properties &key left fill right
                                 &allow-other-keys)
  "Return a status bar about the window being worked in on FRAME.
LEFT and RIGHT are the segments that go from its left end and from its
right end, each a function of the window returning XAML or nil, or a
list (FUNCTION PROPERTIES...) that gives it PROPERTIES as well.  FILL
are segments that go between them and take the room they leave, the
last of them all that is left: what does not fit in it is cut off,
which is what something as long as a message wants.  The rest of
PROPERTIES are properties of the Grid it is, its height and
background for one, and :Foreground, the colour of what it says.

It is a row of its own, so that what it says changing sends the bar and
nothing else."
  (let* ((window (urusi-statusbar-window frame))
         (build (lambda (segments)
                  (delq nil (mapcar (lambda (segment)
                                      (if (functionp segment)
                                          (funcall segment window)
                                        (apply (car segment) window (cdr segment))))
                                    segments))))
         (column (plist-get properties :Grid.Column))
         (row (plist-get properties :Grid.Row))
         (foreground (plist-get properties :Foreground)))
    `(Rows :key "urusi-statusbar" :panel "Grid"
           ,@(when column (list :Grid.Column column))
           ,@(when row (list :Grid.Row row))
           (Grid :key "bar"
                 ,@(urusi-titlebar--without
                    properties '(:left :fill :right :Grid.Column :Grid.Row :Foreground))
                 ;; A Grid has no colour for text to take.  Text and
                 ;; buttons each take the theme's unless they are told
                 ;; otherwise, so they are told here, all at once.
                 (Grid.Resources
                  ,@(when foreground
                      `((Style :TargetType "TextBlock"
                               (Setter :Property "Foreground" :Value ,foreground))))
                  ,@(cl-loop for (key color) on
                             (list "ButtonForeground" foreground
                                   "ButtonForegroundPointerOver" foreground
                                   "ButtonForegroundPressed" foreground
                                   "ButtonBackgroundPointerOver" urusi-statusbar-hover
                                   "ButtonBackgroundPressed" urusi-statusbar-hover
                                   "ButtonBorderBrushPointerOver" "Transparent"
                                   "ButtonBorderBrushPressed" "Transparent")
                             by #'cddr
                             when color
                             collect `(SolidColorBrush :x:Key ,key :Color ,color)))
                 (Grid.ColumnDefinitions
                  (ColumnDefinition :Width "Auto")
                  (ColumnDefinition :Width "*")
                  (ColumnDefinition :Width "Auto"))
                 (StackPanel :Orientation "Horizontal" ,@(funcall build left))
                 ;; Each in a column of its own, the last taking the rest:
                 ;; a StackPanel would give them all the room they ask
                 ;; for, and nothing would ever be cut off.
                 ,(let ((segments (funcall build fill)))
                    `(Grid :Grid.Column 1
                           (Grid.ColumnDefinitions
                            ,@(cl-loop for rest on segments
                                       collect `(ColumnDefinition
                                                 :Width ,(if (cdr rest) "Auto" "*"))))
                           ,@(cl-loop for segment in segments
                                      for column from 0
                                      collect (append segment
                                                      (list :Grid.Column column)))))
                 (StackPanel :Orientation "Horizontal" :Grid.Column 2
                             ,@(funcall build right))))))

(provide 'urusi-statusbar)
;;; urusi-statusbar.el ends here
