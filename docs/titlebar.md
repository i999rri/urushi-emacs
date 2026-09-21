# Title bar

The window's title bar can be drawn in Lisp, from your init file, with whatever height and whatever on it you like. urusi brings the parts in `urusi-titlebar`, and the title bar itself is yours to put together from them.

Without one, the window keeps the title bar Windows gives it.

## How it works

Any element named `urusi-titlebar` is the title bar. While there is one on the screen, the host takes the window's own title bar away, lets that element move the window when it is dragged and maximize it when it is double-clicked, and leaves the controls on it to be clicked.

It goes on the screen like any other component: a function of the frame that returns a tree, listed in `urusi-screen-components` above `urusi-screen-windows`. The windows take whatever height the title bar leaves.

The parts are:

| Part | What it is |
| --- | --- |
| `(urusi-titlebar-title &rest PROPERTIES)` | The window's title, as `frame-title-format` makes it. PROPERTIES go to its `TextBlock`. |
| `(urusi-titlebar-buttons &rest PROPERTIES &key height foreground)` | Minimize, maximize and close, side by side, as Windows draws them. PROPERTIES go to the `StackPanel` they are in. |
| `(urusi-titlebar-button NAME SYMBOL ACTION &rest PROPERTIES)` | One such button, showing SYMBOL (a character of Segoe Fluent Icons) and calling ACTION when clicked. |
| `urusi-titlebar-minimize`, `urusi-titlebar-toggle-maximized`, `urusi-titlebar-close` | What the buttons do, as commands. Closing asks about unsaved buffers first, as `save-buffers-kill-emacs` does. |

Properties given to a part replace the ones it has rather than repeating them.

## Template

A title bar like the one Windows draws, 32 pixels tall, in the colours of a face of its own. Put it in a file on your `load-path`, say `lisp/my-titlebar.el`:

```elisp
;;; my-titlebar.el --- My title bar  -*- lexical-binding: t; -*-

(require 'urusi-screen)
(require 'urusi-titlebar)

(defface my-titlebar
  '((t :inherit default))
  "Face of my title bar."
  :group 'urusi)

(defvar my-titlebar-height 32
  "How tall my title bar is, in the pixels XAML counts in.")

(defun my-titlebar--color (attribute frame)
  (urusi-screen-color (face-attribute 'my-titlebar attribute frame t)))

(defun my-titlebar (frame)
  ;; The colours of FRAME, the one the window shows: a child frame, such
  ;; as a minibuffer floating over it, can have colours of its own.
  (let ((background (my-titlebar--color :background frame))
        (foreground (my-titlebar--color :foreground frame)))
    `(Grid :Name "urusi-titlebar"
           :Height ,my-titlebar-height
           ,@(when background `(:Background ,background))
           (Grid.ColumnDefinitions
            (ColumnDefinition :Width "*")
            (ColumnDefinition :Width "Auto"))
           ,(apply #'urusi-titlebar-title
                   :Margin "12,0,0,0"
                   (when foreground `(:Foreground ,foreground)))
           ,(urusi-titlebar-buttons :Grid.Column 1
                                    :height my-titlebar-height
                                    :foreground foreground))))

(provide 'my-titlebar)
```

And in your init file, only when Emacs is running inside urusi:

```elisp
(add-to-list 'load-path (locate-user-emacs-file "lisp"))

(when (featurep 'urusi)
  (require 'my-titlebar)
  (setq urusi-screen-components
        '(my-titlebar
          urusi-screen-windows)))
```

The recipes below change the template's `my-titlebar`.

## Recipes

### A different height

Make the bar taller, or shorter, and the buttons with it:

```elisp
(defvar my-titlebar-height 48)
```

The title can be larger to go with it: `(urusi-titlebar-title :Margin "16,0,0,0" :FontSize 14)`.

### Fewer buttons, or none

Only a close button:

```elisp
,(urusi-titlebar-button "urusi-close" #xE8BB #'urusi-titlebar-close
                        :Grid.Column 1 :Height my-titlebar-height)
```

No buttons at all: leave `urusi-titlebar-buttons` out. The window can still be moved by the bar, maximized by double-clicking it, and closed with Alt+F4.

### Buttons of your own design

`urusi-titlebar-buttons` is there to be used as it is, and it looks like the buttons of Windows. Buttons that look like something else are any controls that call the same commands:

```elisp
(StackPanel :Grid.Column 1 :Orientation "Horizontal" :Spacing 6 :Margin "0,0,10,0"
            ,@(cl-loop for (color . command) in '(("#febc2e" . urusi-titlebar-minimize)
                                                  ("#28c840" . urusi-titlebar-toggle-maximized)
                                                  ("#ff5f57" . urusi-titlebar-close))
                       collect `(Button :Width 12 :Height 12 :Padding 0
                                        :CornerRadius 6 :BorderThickness 0
                                        :Background ,color
                                        :VerticalAlignment "Center"
                                        :IsTabStop nil :AllowFocusOnInteraction nil
                                        :on-Click ,command)))
```

### No title

Leave `urusi-titlebar-title` out, and let the buttons sit on the right of an empty bar:

```elisp
(defun my-titlebar (_frame)
  `(Grid :Name "urusi-titlebar" :Height ,my-titlebar-height
         ,(urusi-titlebar-buttons :HorizontalAlignment "Right"
                                  :height my-titlebar-height)))
```

### Something of your own on it

Anything put in the bar is part of it, and a control on it can be clicked where the rest of the bar moves the window. A button that finds a file, in a column of its own between the title and the window's buttons:

```elisp
(defun my-titlebar (_frame)
  `(Grid :Name "urusi-titlebar" :Height ,my-titlebar-height
         (Grid.ColumnDefinitions
          (ColumnDefinition :Width "*")
          (ColumnDefinition :Width "Auto")
          (ColumnDefinition :Width "Auto"))
         ,(urusi-titlebar-title :Margin "12,0,0,0")
         (Button :Grid.Column 1 :Content "Open" :Margin "0,0,8,0"
                 :VerticalAlignment "Center"
                 :IsTabStop nil :AllowFocusOnInteraction nil
                 :on-Click ,(lambda () (call-interactively #'find-file)))
         ,(urusi-titlebar-buttons :Grid.Column 2
                                  :height my-titlebar-height)))
```

A command run from a click is one Emacs sees as run from the mouse, so `find-file` asks with the file dialog of Windows rather than in the minibuffer, as it does from the tool bar. Typing `C-x C-f` asks in the minibuffer as it always has.

Give buttons `:IsTabStop nil :AllowFocusOnInteraction nil`, as the parts do: a button that takes the focus takes the keys away from Emacs with it.
