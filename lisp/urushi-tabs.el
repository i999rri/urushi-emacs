;;; urushi-tabs.el --- Tabs drawn by the host, wherever they are put  -*- lexical-binding: t; -*-

;;; Commentary:

;; A strip of tabs is two things that have nothing to do with each
;; other: which tabs there are, and how they look where they are.
;;
;; Which tabs there are is a list, and Emacs already keeps two: the tabs
;; of `tab-bar-mode', one to a frame, each a set of windows, and those of
;; `tab-line-mode', one to a window, each a buffer.  This file reads
;; either into the same shape, a list of plists:
;;
;;   :name     what the tab says
;;   :current  non-nil for the tab that is shown
;;   :face     the face Emacs would draw it in, for a look that follows
;;             the theme
;;   :hover-face  the face Emacs puts over it under the pointer
;;   :select   function of no arguments that shows it
;;   :close    function of no arguments that closes it, or nil
;;   :tab      the tab as its list has it
;;
;; and any list in that shape will do: `urushi-tabs-tab-bar-tabs' and
;; `urushi-tabs-tab-line-tabs' are only the two Emacs has.
;;
;; How they look is `urushi-tabs', which lays the list out in a strip, and
;; `urushi-tabs-tab-function', which draws one tab of it.  The strip is an
;; element like any other, and goes wherever the init file puts it: in a
;; title bar, above the windows, down the side in a panel of the layout.
;;
;;   (defun my-tabs (frame)
;;     (urushi-tabs-tab-bar frame :Height 32))
;;   (setq urushi-screen-components '(my-tabs urushi-screen-windows))
;;
;; The tabs of a window go where Emacs keeps room for them, in the line
;; at the top of the window that `tab-line-format' would draw:
;;
;;   (setq urushi-screen-tab-line-function #'urushi-tabs-tab-line)
;;
;; How tall that line is stays Emacs's to decide, by the face
;; `tab-line'.

;;; Code:

(require 'cl-lib)
(require 'urushi)
(require 'urushi-screen)
(require 'urushi-titlebar)

(defvar tab-bar-tabs-function)
(defvar tab-line-tabs-function)
(defvar tab-line-tab-name-function)
(defvar tab-line-close-tab-function)
(declare-function tab-bar-select-tab "tab-bar" (&optional tab-number))
(declare-function tab-bar-close-tab "tab-bar" (&optional tab-number to-number))
(declare-function tab-line-select-tab-buffer "tab-line" (buffer &optional window))

(defgroup urushi-tabs nil
  "Tabs drawn by the host."
  :group 'urushi)

;;;; Which tabs there are

(defun urushi-tabs-tab-bar-tabs (&optional frame)
  "Return the tabs of `tab-bar-mode' on FRAME, the root frame by default."
  (require 'tab-bar)
  (let ((frame (urushi-root-frame frame)))
    (cl-loop for tab in (funcall tab-bar-tabs-function frame)
             for number from 1
             collect
             (let ((current (eq (car tab) 'current-tab))
                   (number number))
               (list :name (substring-no-properties (alist-get 'name tab))
                     :current current
                     :face (if current 'tab-bar-tab 'tab-bar-tab-inactive)
                     :hover-face 'tab-bar-tab-highlight
                     :select (lambda ()
                               (with-selected-frame frame
                                 (tab-bar-select-tab number)))
                     :close (lambda ()
                              (with-selected-frame frame
                                (tab-bar-close-tab number)))
                     :tab tab)))))

(defun urushi-tabs-tab-line-tabs (&optional window)
  "Return the tabs of `tab-line-mode' in WINDOW, the selected one by default.
They are what `tab-line-tabs-function' says, named as
`tab-line-tab-name-function' names them."
  (require 'tab-line)
  (let ((window (or window (selected-window))))
    (with-selected-window window
      (let ((tabs (funcall tab-line-tabs-function)))
        (mapcar
         (lambda (tab)
           (let* ((buffer (if (bufferp tab) tab (alist-get 'buffer tab)))
                  (current (if (bufferp tab)
                               (eq tab (window-buffer window))
                             (alist-get 'selected tab))))
             (list :name (substring-no-properties
                          (if (bufferp tab)
                              (funcall tab-line-tab-name-function tab tabs)
                            (alist-get 'name tab)))
                   :current current
                   :face (if current 'tab-line-tab-current 'tab-line-tab-inactive)
                   :hover-face 'tab-line-highlight
                   :select (lambda () (urushi-tabs--select-in window tab buffer))
                   :close (lambda () (urushi-tabs--close-in window tab buffer))
                   :tab tab)))
         tabs)))))

(defun urushi-tabs--select-in (window tab buffer)
  "Show TAB, which shows BUFFER, in WINDOW, as clicking it on a tab line does."
  (when (window-live-p window)
    (if buffer
        (tab-line-select-tab-buffer buffer window)
      (when-let* ((select (alist-get 'select tab)))
        (with-selected-window window
          (funcall select))))
    (force-mode-line-update)))

(defun urushi-tabs--close-in (window tab buffer)
  "Close TAB, which shows BUFFER, in WINDOW.
It is closed as its close button on a tab line closes it: the tab's own
way of closing, or `tab-line-close-tab-function'."
  (when (window-live-p window)
    (with-selected-window window
      (let ((close (unless (bufferp tab) (alist-get 'close tab))))
        (cond
         ((functionp close) (funcall close))
         ((eq tab-line-close-tab-function 'kill-buffer)
          (kill-buffer buffer))
         ((eq tab-line-close-tab-function 'bury-buffer)
          (if (eq buffer (current-buffer))
              (bury-buffer)
            (set-window-prev-buffers nil (assq-delete-all buffer (window-prev-buffers)))
            (set-window-next-buffers nil (delq buffer (window-next-buffers)))))
         ((functionp tab-line-close-tab-function)
          (funcall tab-line-close-tab-function tab))))
      (force-mode-line-update))))

;;;; How they look

(defcustom urushi-tabs-tab-function #'urushi-tabs-tab
  "Function that draws one tab.
It takes the tab, a plist as `urushi-tabs' describes, and returns it as
XAML.  Clicking what it returns does nothing by itself: the tab's
:select and :close are what to give the controls in it."
  :type 'function)

(defcustom urushi-tabs-icon-function nil
  "Function that gives a tab its icon, or nil for tabs without one.
It takes the tab, a plist as `urushi-tabs' describes, and returns a
string to draw before its name, or nil.  The string is drawn in the
font family and the colour of the face on it, as Emacs would draw it,
and in the colour of the tab where its face gives none: an icon of a
font of icons, such as the ones `nerd-icons' makes, draws as it is."
  :type '(choice (const :tag "None" nil) function))

(defun urushi-tabs--string-face-attribute (string attribute)
  "Return ATTRIBUTE of the face on the start of STRING, or nil if it has none.
The face can be a face, a plist of attributes, or a list of either, and
an attribute it does not give is looked for in what it inherits."
  (cl-labels ((lookup (face)
                (cond ((null face) nil)
                      ((symbolp face)
                       (and (facep face)
                            (let ((value (face-attribute face attribute nil t)))
                              (unless (eq value 'unspecified) value))))
                      ((keywordp (car face))
                       (or (let ((value (plist-get face attribute)))
                             (unless (eq value 'unspecified) value))
                           (lookup (plist-get face :inherit))))
                      (t (cl-some #'lookup face)))))
    (lookup (get-text-property 0 'face string))))

(defun urushi-tabs-icon (icon &rest properties)
  "Return ICON, a string, as the icon of a tab.
It is drawn in the font family and the colour of the face on it, and
PROPERTIES are more properties of the TextBlock it is in."
  (let ((family (urushi-tabs--string-face-attribute icon :family))
        (foreground (urushi-screen-color
                     (urushi-tabs--string-face-attribute icon :foreground))))
    ;; What the caller says is left out of what is said here rather
    ;; than laid beside it: XAML takes each property once, and a caller
    ;; that says where the icon sits or what colour it is would
    ;; otherwise say it twice, which is an error and not the caller
    ;; winning.
    `(TextBlock :Text ,(urushi-literal (substring-no-properties icon))
                ,@(urushi-titlebar--without
                   `(:VerticalAlignment "Center"
                     ,@(when (stringp family) `(:FontFamily ,family))
                     ,@(when foreground `(:Foreground ,foreground)))
                   (cl-loop for (key _) on properties by #'cddr collect key))
                ,@properties)))

(defun urushi-tabs--face-color (face attribute)
  "Return the colour FACE gives ATTRIBUTE, or nil if it gives none."
  (and face (facep face)
       (urushi-screen-color (face-attribute face attribute nil t))))

(defun urushi-tabs-button-colors (background foreground &optional
                                            hover-background hover-foreground)
  "Return the colours of a button, to go in its Resources.
A button draws itself in BACKGROUND and FOREGROUND, and under the pointer
and pressed in HOVER-BACKGROUND and HOVER-FOREGROUND, which default to
the first two: the colours of its own that WinUI would otherwise give
it there are the theme's, and not the tab's.  Any of them nil is left
as WinUI has it."
  (let ((hover-background (or hover-background background))
        (hover-foreground (or hover-foreground foreground)))
    `(Button.Resources
      ,@(cl-loop for (key color) on
                 (list "ButtonBackgroundPointerOver" hover-background
                       "ButtonBackgroundPressed" hover-background
                       "ButtonForegroundPointerOver" hover-foreground
                       "ButtonForegroundPressed" hover-foreground
                       "ButtonBorderBrushPointerOver" "Transparent"
                       "ButtonBorderBrushPressed" "Transparent")
                 by #'cddr
                 when color
                 collect `(SolidColorBrush :x:Key ,key :Color ,color)))))

(defun urushi-tabs-tab (tab)
  "Return TAB as a tab drawn in the colours of its face.
The tab is a button, with its name and a button that closes it, when it
can be closed.  Neither takes the focus, which would take the keys.

Under the pointer, a tab that is not the current one takes the colours
of its :hover-face over those of its face, as Emacs draws it; the
current one stays as it is."
  (let* ((face (plist-get tab :face))
         (hover (and (not (plist-get tab :current)) (plist-get tab :hover-face)))
         (background (urushi-tabs--face-color face :background))
         (foreground (urushi-tabs--face-color face :foreground))
         (close (plist-get tab :close)))
    `(Button :on-Click ,(plist-get tab :select)
             :Padding ,(if close "10,0,2,0" "10,0,10,0")
             :CornerRadius 0
             :BorderThickness 0
             :VerticalAlignment "Stretch"
             :IsTabStop nil
             :AllowFocusOnInteraction nil
             ,@(when background `(:Background ,background))
             ,@(when foreground `(:Foreground ,foreground))
             ,(urushi-tabs-button-colors
               background foreground
               (urushi-tabs--face-color hover :background)
               (urushi-tabs--face-color hover :foreground))
             (StackPanel :Orientation "Horizontal" :Spacing 4
                         ,@(when-let* ((icon (and urushi-tabs-icon-function
                                                  (funcall urushi-tabs-icon-function tab))))
                             (list (urushi-tabs-icon icon :Margin "0,0,2,0")))
                         (TextBlock :Text ,(urushi-literal (plist-get tab :name))
                                    :VerticalAlignment "Center")
                         ,@(when close
                             `((Button :on-Click ,close
                                       :Content ,(string #xE8BB)
                                       :FontFamily ,urushi-titlebar-symbol-font
                                       :FontSize 8
                                       :Width 20
                                       :Height 20
                                       :Padding 0
                                       :CornerRadius 4
                                       :BorderThickness 0
                                       :Background "Transparent"
                                       :VerticalAlignment "Center"
                                       :IsTabStop nil
                                       :AllowFocusOnInteraction nil
                                       ;; Its own, or it would take the
                                       ;; tab's from the tab around it.
                                       ,(urushi-tabs-button-colors
                                         "Transparent" foreground
                                         urushi-hover-color foreground))))))))

(cl-defun urushi-tabs (tabs &rest properties &key (orientation "Horizontal")
                           (scroll t) &allow-other-keys)
  "Return TABS laid out in a strip, each drawn by `urushi-tabs-tab-function'.
ORIENTATION is \"Horizontal\", the default, or \"Vertical\", for tabs
down the side.  SCROLL non-nil, the default, lets tabs that do not fit
be scrolled to; nil cuts them off, which leaves the space beside the
tabs a title bar's to be dragged by, as a scrolling strip is a control
and is not.  The rest of PROPERTIES are properties of the outermost
element, such as its height or where it goes in the grid around it."
  (let ((strip `(StackPanel :Orientation ,orientation
                            ,@(mapcar (lambda (tab) (funcall urushi-tabs-tab-function tab))
                                      tabs)))
        (properties (urushi-titlebar--without properties '(:orientation :scroll)))
        (horizontal (equal orientation "Horizontal")))
    (if (not scroll)
        `(Grid ,@properties ,strip)
      ;; Merged rather than laid beside: XAML takes each property once,
      ;; and a caller that says how the strip scrolls would otherwise
      ;; say it twice, which is not the later one winning but an error.
      `(ScrollViewer
        ,@(urushi-titlebar--merge
           properties
           `(:IsTabStop nil
             :HorizontalScrollMode ,(if horizontal "Enabled" "Disabled")
             :HorizontalScrollBarVisibility ,(if horizontal "Hidden" "Disabled")
             :VerticalScrollMode ,(if horizontal "Disabled" "Enabled")
             :VerticalScrollBarVisibility ,(if horizontal "Disabled" "Hidden")))
        ,strip))))

;;;; Where they go

(defun urushi-tabs-tab-bar (frame &rest properties)
  "Return the tabs of `tab-bar-mode' on FRAME, as a row of their own.
PROPERTIES are those of `urushi-tabs'.  Being a row, a tab that changes
sends the tabs and nothing else, however much is around them.

It is a component as it is, for `urushi-screen-components', and a part
to put in one of your own, a title bar for one."
  (let ((column (plist-get properties :Grid.Column))
        (row (plist-get properties :Grid.Row)))
    `(Rows :key "urushi-tab-bar" :panel "Grid"
           ,@(when column (list :Grid.Column column))
           ,@(when row (list :Grid.Row row))
           ,(apply #'urushi-tabs (urushi-tabs-tab-bar-tabs frame)
                   :key "tabs"
                   (urushi-titlebar--without properties '(:Grid.Column :Grid.Row))))))

(defun urushi-tabs-tab-line (window _line)
  "Return the tabs of `tab-line-mode' in WINDOW, for its tab line.
It is a `urushi-screen-tab-line-function', and draws them on the
background of the face `tab-line'."
  (let ((background (urushi-tabs--face-color 'tab-line :background)))
    (urushi-tabs (urushi-tabs-tab-line-tabs window)
                :Background (or background "Transparent"))))

(provide 'urushi-tabs)
;;; urushi-tabs.el ends here
