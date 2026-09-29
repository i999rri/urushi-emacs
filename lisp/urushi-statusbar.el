;;; urushi-statusbar.el --- A status bar drawn by the host  -*- lexical-binding: t; -*-

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
;;     (urushi-statusbar frame
;;                      :left '(urushi-statusbar-vc urushi-statusbar-buffer)
;;                      :right '((urushi-statusbar-position :FontSize 12)
;;                               urushi-statusbar-major-mode)
;;                      :Height 24))
;;   (setq urushi-screen-components '(urushi-screen-windows my-statusbar))
;;
;; A segment of your own draws itself, or uses `urushi-statusbar-text' and
;; `urushi-statusbar-button', which look like the rest.

;;; Code:

(require 'cl-lib)
(require 'urushi)
(require 'urushi-screen)
(require 'urushi-titlebar)
(require 'warnings)

(declare-function flymake-diagnostics "flymake" (&optional beg end))
(declare-function flymake-diagnostic-type "flymake" (diag))
(declare-function flymake--severity "flymake" (type))
(declare-function flymake-show-buffer-diagnostics "flymake" ())

(defgroup urushi-statusbar nil
  "A status bar drawn by the host."
  :group 'urushi)

(defun urushi-statusbar-window (&optional frame)
  "Return the window the bar is about, on FRAME's root frame.
That is the selected window, or while the minibuffer is being typed in,
the window that was selected before, which is what whatever is being
typed is about."
  (let ((window (selected-window)))
    (if (and (window-minibuffer-p window) (minibuffer-selected-window))
        (minibuffer-selected-window)
      (if (window-minibuffer-p window)
          (frame-selected-window (urushi-root-frame frame))
        window))))

;;;; What a segment looks like

(defun urushi-statusbar-text (text &rest properties)
  "Return TEXT as a segment of the bar.
PROPERTIES are more properties of the TextBlock it is in, and override
the ones it has."
  `(TextBlock :Text ,(urushi-literal text)
              ,@(urushi-titlebar--merge
                 properties
                 '(:VerticalAlignment "Center"
                   :Margin "8,0,8,0"
                   :TextTrimming "CharacterEllipsis"))))

(defun urushi-statusbar-button (text action &rest properties)
  "Return TEXT as a segment of the bar that does ACTION when clicked.
PROPERTIES are more properties of the Button, and override the ones it
has.  It does not take the focus, which would take the keys."
  `(Button :Content ,(urushi-literal text)
           :on-Click ,action
           ,@(urushi-titlebar--merge
              properties
              '(:VerticalAlignment "Stretch"
                :Padding "8,0,8,0"
                :CornerRadius 0
                :BorderThickness 0
                :Background "Transparent"
                :IsTabStop nil
                :AllowFocusOnInteraction nil))))

;;;; The segments a mode line usually has

(defun urushi-statusbar-buffer (window &rest properties)
  "The name of the buffer in WINDOW, with a dot when it is not saved.
PROPERTIES are those of `urushi-statusbar-text'."
  (let ((buffer (window-buffer window)))
    (apply #'urushi-statusbar-text
           (concat (buffer-name buffer)
                   (when (and (buffer-file-name buffer) (buffer-modified-p buffer))
                     " ●"))
           properties)))

(defun urushi-statusbar-position (window &rest properties)
  "Where the point of WINDOW is, as its line and column.
PROPERTIES are those of `urushi-statusbar-text'."
  (with-current-buffer (window-buffer window)
    (let ((point (window-point window)))
      (apply #'urushi-statusbar-text
             (format "Ln %d, Col %d"
                     (line-number-at-pos point)
                     (1+ (save-excursion (goto-char point) (current-column))))
             properties))))

(defun urushi-statusbar-major-mode (window &rest properties)
  "The name of the major mode of the buffer in WINDOW.
PROPERTIES are those of `urushi-statusbar-text'.
A mode of CC Mode puts how it is set after its name, \"C#//l\" for C#
with line comments and electric keys, which is not the language's
name: it is left out, as CC Mode itself finds the name."
  (let* ((buffer (window-buffer window))
         (name (string-trim (format-mode-line mode-name nil window buffer))))
    (apply
     #'urushi-statusbar-text
     (if (and (boundp 'c-buffer-is-cc-mode)
              (buffer-local-value 'c-buffer-is-cc-mode buffer)
              (string-match "\\`\\([^/]+\\)/" name))
         (match-string 1 name)
       name)
     properties)))

(defun urushi-statusbar-encoding (window &rest properties)
  "How the file in WINDOW is encoded, and how its lines end.
PROPERTIES are those of `urushi-statusbar-text'."
  (with-current-buffer (window-buffer window)
    (when buffer-file-name
      (let* ((system buffer-file-coding-system)
             (name (symbol-name (coding-system-base system)))
             (eol (pcase (coding-system-eol-type system)
                    (0 "LF") (1 "CRLF") (2 "CR"))))
        (apply #'urushi-statusbar-text
               (string-join (delq nil (list (upcase (string-remove-suffix
                                                     "-with-signature" name))
                                            eol))
                            "  ")
               properties)))))

(defun urushi-statusbar-vc (window &rest properties)
  "The branch the file in WINDOW is on, as version control says it.
PROPERTIES are those of `urushi-statusbar-text'."
  (when-let* ((vc (buffer-local-value 'vc-mode (window-buffer window)))
              (vc (string-trim (substring-no-properties vc)))
              ;; " Git-main", " Git:main": the branch after the backend.
              (branch (and (string-match "\\`[^-:@]+[-:@]\\(.*\\)\\'" vc)
                           (match-string 1 vc))))
    (apply #'urushi-statusbar-text (concat (string #x2387) " " branch) properties)))

(defvar urushi-statusbar--minibuffer-message nil
  "What was said while the minibuffer was being typed in, or nil.")

(defvar urushi-statusbar--minibuffer-message-timer nil
  "Timer that forgets `urushi-statusbar--minibuffer-message'.")

(defcustom urushi-statusbar-minibuffer-message-timeout 3
  "How long, in seconds, a message said while typing in the minibuffer stays."
  :type 'number)

(defun urushi-statusbar-take-minibuffer-message (message)
  "Say MESSAGE in the status bar if the minibuffer is being typed in.
It is a function for `set-message-functions', to go before the others:

  (add-hook \\='set-message-functions
            #\\='urushi-statusbar-take-minibuffer-message)

While the minibuffer is being typed in, Emacs puts a message after what
is typed, where it wraps in a minibuffer of a fixed width and runs into
the question being asked.  This takes it instead, for
`urushi-statusbar-message' to say, and leaves the minibuffer as it is.
Otherwise it leaves MESSAGE to the others, and to the echo area."
  (when (active-minibuffer-window)
    (setq urushi-statusbar--minibuffer-message message)
    (when (timerp urushi-statusbar--minibuffer-message-timer)
      (cancel-timer urushi-statusbar--minibuffer-message-timer))
    (setq urushi-statusbar--minibuffer-message-timer
          (run-at-time urushi-statusbar-minibuffer-message-timeout nil
                       #'urushi-statusbar--forget-minibuffer-message))
    (add-hook 'minibuffer-exit-hook #'urushi-statusbar--forget-minibuffer-message)
    (urushi-screen--after-command)
    t))

(defun urushi-statusbar--forget-minibuffer-message ()
  "Stop saying what was said while the minibuffer was being typed in."
  (remove-hook 'minibuffer-exit-hook #'urushi-statusbar--forget-minibuffer-message)
  (when (timerp urushi-statusbar--minibuffer-message-timer)
    (cancel-timer urushi-statusbar--minibuffer-message-timer))
  (setq urushi-statusbar--minibuffer-message nil
        urushi-statusbar--minibuffer-message-timer nil)
  (urushi-screen--after-command))

(defun urushi-statusbar-message (_window &rest properties)
  "What Emacs is saying in the echo area, while it says it.
Or what it said while the minibuffer was being typed in, when
`urushi-statusbar-take-minibuffer-message' took that.  Only its first
line, cut off where there is no more room when it is in the FILL of
`urushi-statusbar'.  PROPERTIES are those of `urushi-statusbar-text'."
  (when-let* ((message (or urushi-statusbar--minibuffer-message (current-message)))
              (line (car (split-string (substring-no-properties message) "\n")))
              ((not (string-empty-p line))))
    (apply #'urushi-statusbar-text line properties)))

(defun urushi-statusbar-diagnostics (window &rest properties)
  "How many errors and warnings Flymake has found in WINDOW's buffer.
Clicking it lists them.  PROPERTIES are those of `urushi-statusbar-button'."
  (with-current-buffer (window-buffer window)
    (when (bound-and-true-p flymake-mode)
      (let ((errors 0) (warnings 0))
        (dolist (diagnostic (flymake-diagnostics))
          (let ((severity (flymake--severity (flymake-diagnostic-type diagnostic))))
            (cond ((>= severity (warning-numeric-level :error)) (cl-incf errors))
                  ((>= severity (warning-numeric-level :warning)) (cl-incf warnings)))))
        (let ((buffer (current-buffer)))
          (apply #'urushi-statusbar-button
                 (format "%c %d  %c %d" #x2297 errors #x26A0 warnings)
                 (lambda ()
                   (with-current-buffer buffer
                     (flymake-show-buffer-diagnostics)))
                 properties))))))

;;;; The bar

(cl-defun urushi-statusbar (frame &rest properties &key left fill right
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
  (let* ((window (urushi-statusbar-window frame))
         (build (lambda (segments)
                  (delq nil (mapcar (lambda (segment)
                                      (if (functionp segment)
                                          (funcall segment window)
                                        (apply (car segment) window (cdr segment))))
                                    segments))))
         (column (plist-get properties :Grid.Column))
         (row (plist-get properties :Grid.Row))
         (foreground (plist-get properties :Foreground)))
    `(Rows :key "urushi-statusbar" :panel "Grid"
           ,@(when column (list :Grid.Column column))
           ,@(when row (list :Grid.Row row))
           (Grid :key "bar"
                 ,@(urushi-titlebar--without
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
                                   "ButtonBackgroundPointerOver" urushi-hover-color
                                   "ButtonBackgroundPressed" urushi-hover-color
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

(provide 'urushi-statusbar)
;;; urushi-statusbar.el ends here
