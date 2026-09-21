# Tabs

Tabs can be drawn by the host, as controls of their own, from the lists of tabs Emacs already keeps. urusi brings the parts in `urusi-tabs`: which tabs there are, and a strip to lay them out in. Where the strip goes and what a tab looks like are yours to decide.

Without them, tabs are drawn as Emacs draws them, as text.

## How it works

A strip of tabs is two things that have nothing to do with each other: which tabs there are, and how they look where they are.

Which tabs there are is a list of plists, one for each tab:

| Key | What it is |
| --- | --- |
| `:name` | What the tab says. |
| `:current` | Non-nil for the tab that is shown. |
| `:face` | The face Emacs would draw it in, for a look that follows the theme. |
| `:hover-face` | The face Emacs puts over it under the pointer. |
| `:select` | A function of no arguments that shows it. |
| `:close` | A function of no arguments that closes it, or nil when it cannot be closed. |
| `:tab` | The tab as its own list has it. |

Emacs keeps two such lists, and urusi reads either one into this shape. Any list in the same shape does as well.

How they look is the strip, `urusi-tabs`, which lays the list out, and `urusi-tabs-tab-function`, which draws one tab of it. The strip is an element like any other, and goes wherever you put it.

The parts are:

| Part | What it is |
| --- | --- |
| `(urusi-tabs-tab-bar-tabs &optional FRAME)` | The tabs of `tab-bar-mode`, one to a frame, each a set of windows. |
| `(urusi-tabs-tab-line-tabs &optional WINDOW)` | The tabs of `tab-line-mode`, one to a window, each a buffer, as `tab-line-tabs-function` lists them and `tab-line-tab-name-function` names them. Closing one does what `tab-line-close-tab-function` says. |
| `(urusi-tabs TABS &rest PROPERTIES &key orientation scroll)` | TABS laid out in a strip. ORIENTATION is `"Horizontal"` or `"Vertical"`. SCROLL nil cuts off the tabs that do not fit rather than letting them be scrolled to. PROPERTIES go to the outermost element. |
| `urusi-tabs-tab-function` | The function that draws one tab. The one urusi brings, `urusi-tabs-tab`, is a button in the colours of the tab's faces, with a button that closes it. |
| `urusi-tabs-icon-function` | A function of a tab that returns its icon, a string, or nil. The icon is drawn before the name, in the font family and colour of the face on it, and in the tab's colour where the face gives none. |
| `(urusi-tabs-icon ICON &rest PROPERTIES)` | ICON drawn as `urusi-tabs-tab` draws it, for tabs of your own design. |
| `(urusi-tabs-button-colors BACKGROUND FOREGROUND &optional HOVER-BACKGROUND HOVER-FOREGROUND)` | Colours for a button, to go in its `Button.Resources`: the ones WinUI would give it under the pointer are the theme's, not yours. |
| `(urusi-tabs-tab-bar FRAME &rest PROPERTIES)` | The tabs of `tab-bar-mode` as a row of their own, ready to be a component. PROPERTIES are those of `urusi-tabs`. |
| `(urusi-tabs-tab-line WINDOW LINE)` | The tabs of `tab-line-mode`, for `urusi-screen-tab-line-function`. |

### The tabs of a window

`tab-line-mode` keeps a line at the top of each window for its tabs. Set `urusi-screen-tab-line-function`, and that line is drawn by a function of yours instead of as text:

```elisp
(setq urusi-screen-tab-line-function #'urusi-tabs-tab-line)
```

The function is called with the window and its tab line, and returns what fills the room Emacs kept. How much room that is stays Emacs's to say, by the face `tab-line`: the text below the line is laid out by Emacs, and drawing the line taller would draw it over the text.

To make the line taller, give the face room above and below:

```elisp
(set-face-attribute 'tab-line nil :box '(:line-width (1 . 6) :style flat-button))
```

The first number is the room on either side and cannot be 0; drawn in the background of the face, 1 is not seen.

## Template

Buffer tabs above each window, as Visual Studio has them, in a file on your `load-path`, say `lisp/my-tabs.el`. The current tab is filled, and a line runs along the bottom of the strip in the same colour, joining the tab to the text below it:

```elisp
;;; my-tabs.el --- My tabs  -*- lexical-binding: t; -*-

(require 'urusi-screen)
(require 'urusi-tabs)

(defun my-tabs--color (face attribute)
  (urusi-screen-color (face-attribute face attribute nil t)))

(defun my-tab-line (window _line)
  `(Border :BorderBrush ,(or (my-tabs--color 'tab-line-tab-current :background)
                             "Transparent")
           :BorderThickness "0,0,0,2"
           :Background ,(or (my-tabs--color 'tab-line :background) "Transparent")
           ,(urusi-tabs (urusi-tabs-tab-line-tabs window))))

(provide 'my-tabs)
```

And in your init file:

```elisp
(global-tab-line-mode 1)

(when (featurep 'urusi)
  (require 'my-tabs)
  (setq urusi-screen-tab-line-function #'my-tab-line))
```

The colours come from the faces `tab-line`, `tab-line-tab-current`, `tab-line-tab-inactive` and `tab-line-highlight`, so a theme that sets them sets the tabs, in urusi and out of it.

## Recipes

### Which buffers a window has tabs for

That is `tab-line-mode`'s to say, in `tab-line-tabs-function`, and urusi draws whatever it lists. The buffers of the project the window's buffer is in, in the order they were opened:

```elisp
(defun my-tab-line-tabs ()
  (let ((project (project-current)))
    (seq-filter (lambda (buffer)
                  (and (buffer-file-name buffer)
                       (equal (with-current-buffer buffer (project-current))
                              project)))
                (reverse (buffer-list)))))

(setq tab-line-tabs-function #'my-tab-line-tabs)
```

`buffer-list` changes its order as buffers are shown, so tabs listed straight from it move about. To keep them where they were opened, give each buffer a number the first time it is listed and sort by it.

Closing a tab buries its buffer by default. To have it closed, as other editors do:

```elisp
(setq tab-line-close-tab-function #'kill-buffer)
```

A buffer that is not saved asks first.

### Icons

`urusi-tabs-icon-function` gives each tab an icon. The icon of the file's kind, from the package `nerd-icons`, in the colour of the tab's text, which stays readable on the current tab whatever its colour:

```elisp
(defun my-tab-icon (tab)
  (let ((buffer (let ((tab (plist-get tab :tab)))
                  (if (bufferp tab) tab (alist-get 'buffer tab)))))
    (when (and (buffer-live-p buffer) (require 'nerd-icons nil t))
      (let ((icon (with-current-buffer buffer (nerd-icons-icon-for-buffer))))
        (when (and (stringp icon) (< 0 (length icon)))
          ;; The glyph in the icons' font, without the colour nerd-icons gives it.
          (propertize (substring-no-properties icon)
                      'face `(:family ,nerd-icons-font-family)))))))

(setq urusi-tabs-icon-function #'my-tab-icon)
```

Return the icon as `nerd-icons` makes it to keep its colours.

### Tabs of your own design

`urusi-tabs-tab-function` draws one tab. Anything that calls the tab's `:select` when clicked, and its `:close` if it has one, is a tab. A rounded tab with no close button, the current one bold:

```elisp
(defun my-tab (tab)
  `(Button :on-Click ,(plist-get tab :select)
           :Content ,(urusi-literal (plist-get tab :name))
           :Margin "2,4,2,4"
           :Padding "12,0,12,0"
           :CornerRadius 6
           :BorderThickness 0
           ,@(when (plist-get tab :current) '(:FontWeight "Bold"))
           :IsTabStop nil
           :AllowFocusOnInteraction nil))

(setq urusi-tabs-tab-function #'my-tab)
```

Give buttons `:IsTabStop nil :AllowFocusOnInteraction nil`: a button that takes the focus takes the keys away from Emacs with it.

### The tabs of the tab bar, above everything

The tabs of `tab-bar-mode` are a component as they are:

```elisp
(defun my-tab-bar (frame)
  (urusi-tabs-tab-bar frame :Height 32))

(setq urusi-screen-components '(my-tab-bar urusi-screen-windows))
```

Keep Emacs from drawing them a second time with `(setq tab-bar-show nil)`: the tabs are still there, and `tab-bar-tabs-function` still lists them.

### In the title bar

The strip goes in a title bar like anything else. With `:scroll nil` it is not a control, and the space beside the tabs still moves the window when it is dragged:

```elisp
,(urusi-tabs-tab-bar frame :scroll nil :Grid.Column 1)
```

### Down the side

A strip stands on end with `:orientation "Vertical"`, and scrolls up and down. Put it in a panel of the layout (see [layout](layout.md)) to have the tabs down the left of the window:

```elisp
(urusi-tabs (urusi-tabs-tab-bar-tabs) :orientation "Vertical" :Width 200)
```
