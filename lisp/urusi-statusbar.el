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
;; a mode line usually says:
;;
;;   (defun my-statusbar (frame)
;;     (urusi-statusbar frame
;;                      :left '(urusi-statusbar-vc urusi-statusbar-buffer)
;;                      :right '(urusi-statusbar-position
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

(defun urusi-statusbar-buffer (window)
  "The name of the buffer in WINDOW, with a dot when it is not saved."
  (let ((buffer (window-buffer window)))
    (urusi-statusbar-text
     (concat (buffer-name buffer)
             (when (and (buffer-file-name buffer) (buffer-modified-p buffer))
               " ●")))))

(defun urusi-statusbar-position (window)
  "Where the point of WINDOW is, as its line and column."
  (with-current-buffer (window-buffer window)
    (let ((point (window-point window)))
      (urusi-statusbar-text
       (format "Ln %d, Col %d"
               (line-number-at-pos point)
               (1+ (save-excursion (goto-char point) (current-column))))))))

(defun urusi-statusbar-major-mode (window)
  "The name of the major mode of the buffer in WINDOW.
A mode of CC Mode puts how it is set after its name, \"C#//l\" for C#
with line comments and electric keys, which is not the language's
name: it is left out, as CC Mode itself finds the name."
  (let* ((buffer (window-buffer window))
         (name (string-trim (format-mode-line mode-name nil window buffer))))
    (urusi-statusbar-text
     (if (and (boundp 'c-buffer-is-cc-mode)
              (buffer-local-value 'c-buffer-is-cc-mode buffer)
              (string-match "\\`\\([^/]+\\)/" name))
         (match-string 1 name)
       name))))

(defun urusi-statusbar-encoding (window)
  "How the file in WINDOW is encoded, and how its lines end."
  (with-current-buffer (window-buffer window)
    (when buffer-file-name
      (let* ((system buffer-file-coding-system)
             (name (symbol-name (coding-system-base system)))
             (eol (pcase (coding-system-eol-type system)
                    (0 "LF") (1 "CRLF") (2 "CR"))))
        (urusi-statusbar-text
         (string-join (delq nil (list (upcase (string-remove-suffix "-with-signature" name))
                                      eol))
                      "  "))))))

(defun urusi-statusbar-vc (window)
  "The branch the file in WINDOW is on, as version control says it."
  (when-let* ((vc (buffer-local-value 'vc-mode (window-buffer window)))
              (vc (string-trim (substring-no-properties vc)))
              ;; " Git-main", " Git:main": the branch after the backend.
              (branch (and (string-match "\\`[^-:@]+[-:@]\\(.*\\)\\'" vc)
                           (match-string 1 vc))))
    (urusi-statusbar-text (concat (string #x2387) " " branch))))

(defun urusi-statusbar-diagnostics (window)
  "How many errors and warnings Flymake has found in WINDOW's buffer.
Clicking it lists them."
  (with-current-buffer (window-buffer window)
    (when (bound-and-true-p flymake-mode)
      (let ((errors 0) (warnings 0))
        (dolist (diagnostic (flymake-diagnostics))
          (let ((severity (flymake--severity (flymake-diagnostic-type diagnostic))))
            (cond ((>= severity (warning-numeric-level :error)) (cl-incf errors))
                  ((>= severity (warning-numeric-level :warning)) (cl-incf warnings)))))
        (let ((buffer (current-buffer)))
          (urusi-statusbar-button
           (format "%c %d  %c %d" #x2297 errors #x26A0 warnings)
           (lambda ()
             (with-current-buffer buffer
               (flymake-show-buffer-diagnostics)))))))))

;;;; The bar

(cl-defun urusi-statusbar (frame &rest properties &key left right
                                 &allow-other-keys)
  "Return a status bar about the window being worked in on FRAME.
LEFT and RIGHT are the segments that go from its left end and from its
right end, each a function of the window returning XAML or nil.  The
rest of PROPERTIES are properties of the Grid it is, its height and
background for one, and :Foreground, the colour of what it says.

It is a row of its own, so that what it says changing sends the bar and
nothing else."
  (let* ((window (urusi-statusbar-window frame))
         (build (lambda (segments)
                  (delq nil (mapcar (lambda (segment) (funcall segment window))
                                    segments))))
         (column (plist-get properties :Grid.Column))
         (row (plist-get properties :Grid.Row))
         (foreground (plist-get properties :Foreground)))
    `(Rows :key "urusi-statusbar" :panel "Grid"
           ,@(when column (list :Grid.Column column))
           ,@(when row (list :Grid.Row row))
           (Grid :key "bar"
                 ,@(urusi-titlebar--without
                    properties '(:left :right :Grid.Column :Grid.Row :Foreground))
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
                  (ColumnDefinition :Width "*")
                  (ColumnDefinition :Width "Auto"))
                 (StackPanel :Orientation "Horizontal" ,@(funcall build left))
                 (StackPanel :Orientation "Horizontal" :Grid.Column 1
                             ,@(funcall build right))))))

(provide 'urusi-statusbar)
;;; urusi-statusbar.el ends here
