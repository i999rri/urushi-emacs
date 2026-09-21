;;; urusi-titlebar.el --- A title bar drawn in Lisp  -*- lexical-binding: t; -*-

;;; Commentary:

;; A component for `urusi-screen-components' that draws the window's
;; title bar, with the title and buttons to minimize, maximize and
;; close.  It is not on the screen unless it is asked for:
;;
;;   (require 'urusi-titlebar)
;;   (setq urusi-screen-components
;;         '(urusi-titlebar urusi-screen-windows))
;;
;; What it returns is named urusi-titlebar, which is what tells the host
;; to take the window's own title bar away and to let this one move the
;; window.  Its buttons are the size and the symbols of the ones Windows
;; draws, and its colours are those of the face `urusi-titlebar'.

;;; Code:

(require 'urusi)
(require 'urusi-screen)

(defgroup urusi-titlebar nil
  "A title bar drawn in Lisp."
  :group 'urusi)

(defface urusi-titlebar
  '((t :inherit default))
  "Face of the title bar.
Its background is the bar's, and its foreground the title's and the
buttons'.")

(defcustom urusi-titlebar-height 32
  "How tall the title bar is, in the pixels XAML counts in.
It is the height Windows draws its own at."
  :type 'number)

(defconst urusi-titlebar--symbol-font "Segoe Fluent Icons, Segoe MDL2 Assets"
  "Font the buttons draw their symbols in.
Windows draws the buttons of its own title bars in the first, and the
second is the one Windows 10 has, with the same symbols in the same
places.")

(defun urusi-titlebar--color (attribute)
  "Return ATTRIBUTE of the face `urusi-titlebar' as a XAML colour."
  (urusi-screen-color (face-attribute 'urusi-titlebar attribute nil t)))

(defun urusi-titlebar--button (name symbol action)
  "Return a button NAME showing SYMBOL, doing ACTION when clicked.
SYMBOL is a character of `urusi-titlebar--symbol-font'."
  (let ((foreground (urusi-titlebar--color :foreground)))
    `(Button :Name ,name
             :Content ,(string symbol)
             :FontFamily ,urusi-titlebar--symbol-font
             :FontSize 10
             :Width 46
             :Height ,urusi-titlebar-height
             :Padding 0
             :CornerRadius 0
             :BorderThickness 0
             :Background "Transparent"
             ;; Clicked, not focused: a button that took the focus would
             ;; take the keys with it, and the space bar would press it.
             :IsTabStop nil
             :AllowFocusOnInteraction nil
             ,@(when foreground `(:Foreground ,foreground))
             :on-Click ,action)))

(defun urusi-titlebar--maximized-p ()
  "Return non-nil if the window fills the screen."
  (member urusi-window-state '("maximized" "fullscreen")))

(defun urusi-titlebar--toggle-maximized ()
  "Maximize the window, or restore it if it is maximized already."
  (urusi-call "window.state"
              (list :state (if (urusi-titlebar--maximized-p) "normal" "maximized"))))

(defun urusi-titlebar (_frame)
  "Return the title bar, for `urusi-screen-components'.
Closing leaves Emacs the way `save-buffers-kill-emacs' does, asking
about the buffers that are not saved first."
  (let ((background (urusi-titlebar--color :background))
        (foreground (urusi-titlebar--color :foreground)))
    `(Grid :Name "urusi-titlebar"
           :Height ,urusi-titlebar-height
           ,@(when background `(:Background ,background))
           (Grid.ColumnDefinitions
            (ColumnDefinition :Width "*")
            (ColumnDefinition :Width "Auto"))
           ;; A row of its own, so that a new title changes the title and
           ;; nothing else.
           (Rows :key "titlebar-title" :panel "Grid"
                 (TextBlock :key "title"
                            :Text ,(format-mode-line frame-title-format)
                            :VerticalAlignment "Center"
                            :Margin "12,0,0,0"
                            :FontSize 12
                            :TextTrimming "CharacterEllipsis"
                            ,@(when foreground `(:Foreground ,foreground))))
           (StackPanel :Grid.Column 1 :Orientation "Horizontal"
                       ,(urusi-titlebar--button
                         "urusi-minimize" #xE921
                         (lambda () (urusi-call "window.state" '(:state "minimized"))))
                       ,(urusi-titlebar--button
                         "urusi-maximize"
                         (if (urusi-titlebar--maximized-p) #xE923 #xE922)
                         #'urusi-titlebar--toggle-maximized)
                       ,(urusi-titlebar--button
                         "urusi-close" #xE8BB
                         ;; Not from the handler: what it asks waits for
                         ;; the host's messages, which are read from where
                         ;; the handler runs.
                         (lambda () (run-at-time 0 nil #'save-buffers-kill-emacs)))))))

(provide 'urusi-titlebar)
;;; urusi-titlebar.el ends here
