;;; urushi-titlebar.el --- What a title bar drawn in Lisp is made of  -*- lexical-binding: t; -*-

;;; Commentary:

;; The parts of a title bar, for an init file to make its own out of.
;; How tall it is and what else is on it are the init file's to say, so
;; nothing here is a title bar by itself.
;;
;; Any element named urushi-titlebar is one: the host takes the window's
;; own title bar away while there is such an element on the screen, lets
;; it move the window as a title bar does, and leaves the controls on it
;; to be clicked.  Put it in `urushi-screen-components' like anything else:
;;
;;   (require 'urushi-titlebar)
;;
;;   (defun my-titlebar (_frame)
;;     `(Grid :Name "urushi-titlebar" :Height 40
;;            (Grid.ColumnDefinitions
;;             (ColumnDefinition :Width "*")
;;             (ColumnDefinition :Width "Auto"))
;;            ,(urushi-titlebar-title :Margin "12,0,0,0")
;;            ,(urushi-titlebar-buttons :height 40 :Grid.Column 1)))
;;
;;   (setq urushi-screen-components '(my-titlebar urushi-screen-windows))

;;; Code:

(require 'cl-lib)
(require 'urushi)
(require 'urushi-frame)
(require 'urushi-screen)

(defconst urushi-titlebar-symbol-font "Segoe Fluent Icons, Segoe MDL2 Assets"
  "Font the buttons of a title bar draw their symbols in.
Windows draws the buttons of its own title bars in the first, and the
second is the one Windows 10 has, with the same symbols in the same
places.")

;;;; What the buttons do

(defun urushi-titlebar-maximized-p ()
  "Return non-nil if the window fills the screen."
  (member urushi-window-state '("maximized" "fullscreen")))

(defun urushi-titlebar-minimize ()
  "Minimize the window."
  (interactive)
  (urushi-call "window.state" '(:state "minimized")))

(defun urushi-titlebar-toggle-maximized ()
  "Maximize the window, or restore it if it is maximized already."
  (interactive)
  (urushi-call "window.state"
              (list :state (if (urushi-titlebar-maximized-p) "normal" "maximized"))))

(defun urushi-titlebar-close ()
  "Leave Emacs, asking about the buffers that are not saved first.
This is what closing the window does, and it does the same thing:
`urushi-frame-close-function' is asked either way, so a button drawn
here and the window's own close are not two different ways to leave."
  (interactive)
  ;; Not from a button's handler: what it asks waits for the host's
  ;; messages, which are read from where the handler runs.
  (run-at-time 0 nil urushi-frame-close-function))

;;;; The parts

(defun urushi-titlebar-title (&rest properties)
  "Return the window's title, as `frame-title-format' makes it.
PROPERTIES are more properties of the TextBlock it is drawn in, and
override the ones it has.

It is a row of its own, so that a new title sends the title and nothing
else; only one can be on the screen."
  (let ((column (plist-get properties :Grid.Column)))
    ;; The column is the panel's to be in: the TextBlock is in the panel.
    `(Rows :key "urushi-titlebar-title" :panel "Grid"
           ,@(when column (list :Grid.Column column))
           (TextBlock :key "title"
                      :Text ,(urushi-literal
                             (format-mode-line frame-title-format nil
                                               (frame-selected-window (urushi-root-frame))))
                      ,@(urushi-titlebar--merge
                         (urushi-titlebar--without properties '(:Grid.Column))
                         '(:VerticalAlignment "Center"
                           :FontSize 12
                           :TextTrimming "CharacterEllipsis"))))))

(defun urushi-titlebar-button (name symbol action &rest properties)
  "Return a title bar button NAME showing SYMBOL, doing ACTION when clicked.
SYMBOL is a character of `urushi-titlebar-symbol-font'.  PROPERTIES are
more properties of the Button, and override the ones it has.

It is the size and the look of the buttons Windows draws: 46 wide and
the height of the bar it is on, flat until the pointer is over it."
  `(Button :Name ,name
           :Content ,(string symbol)
           :on-Click ,action
           ,@(urushi-titlebar--merge
              properties
              `(:FontFamily ,urushi-titlebar-symbol-font
                :FontSize 10
                :Width 46
                :Padding 0
                :CornerRadius 0
                :BorderThickness 0
                :Background "Transparent"
                ;; Clicked, not focused: a button that took the focus
                ;; would take the keys with it, and the space bar would
                ;; press it.
                :IsTabStop nil
                :AllowFocusOnInteraction nil))))

(cl-defun urushi-titlebar-buttons (&rest properties &key height foreground
                                        &allow-other-keys)
  "Return the minimize, maximize and close buttons, side by side.
HEIGHT is how tall they are, in the pixels XAML counts in, and
FOREGROUND the colour of their symbols.  The rest of PROPERTIES are
properties of the StackPanel they are in, such as where it goes in the
grid around it."
  (let ((button (lambda (name symbol action)
                  (apply #'urushi-titlebar-button name symbol action
                         (append (when height (list :Height height))
                                 (when foreground (list :Foreground foreground)))))))
    `(StackPanel :Orientation "Horizontal"
                 ,@(urushi-titlebar--without properties '(:height :foreground))
                 ,(funcall button "urushi-minimize" #xE921 #'urushi-titlebar-minimize)
                 ,(funcall button "urushi-maximize"
                           (if (urushi-titlebar-maximized-p) #xE923 #xE922)
                           #'urushi-titlebar-toggle-maximized)
                 ,(funcall button "urushi-close" #xE8BB #'urushi-titlebar-close))))

(defun urushi-titlebar--without (plist keys)
  "Return PLIST without the properties named in KEYS."
  (cl-loop for (key value) on plist by #'cddr
           unless (memq key keys) append (list key value)))

(defun urushi-titlebar--merge (properties defaults)
  "Return PROPERTIES, and those of DEFAULTS that PROPERTIES does not name.
XAML takes each property once, so one given twice is an error rather
than the later one winning.

What an element is rather than how it is asked to look -- the text of
a TextBlock, the content of a Button -- is written before this rather
than among the defaults, so that it stays at the front of the element
whatever a caller adds."
  (append properties
          (urushi-titlebar--without
           defaults (cl-loop for (key _) on properties by #'cddr collect key))))

(provide 'urushi-titlebar)
;;; urushi-titlebar.el ends here
