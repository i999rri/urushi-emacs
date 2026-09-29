# Status bar

One bar for the whole window, as most applications have, can say what the mode line of each window says: where the point is, what the buffer is, whether anything is wrong with it. urushi brings the bar and the segments a mode line usually has in `urushi-statusbar`, and which segments go on it, and where the bar goes, are yours to decide.

Without one, each window keeps its mode line.

## How it works

The bar is lists of segments: one from its left end, one from its right, and one between them that takes the room the others leave, where what does not fit is cut off. A segment is a function of the window the bar is about, returning XAML, or nil for nothing. Written as a list, `(FUNCTION PROPERTIES...)`, it is given PROPERTIES as well, which the segments urushi brings put on what they draw.

The window the bar is about is the selected one, or while the minibuffer is being typed in, the one selected before: what is being typed is about that.

The bar is a row of its own, so that what it says changing sends the bar and nothing else. It goes on the screen like any other component, in `urushi-screen-components`.

The parts are:

| Part | What it is |
| --- | --- |
| `(urushi-statusbar FRAME &rest PROPERTIES &key left fill right)` | The bar, with the segments LEFT and RIGHT at its ends and FILL between them, the last of FILL taking what is left. PROPERTIES go to the `Grid` it is, its height and background for one, and `:Foreground` is the colour of what it says. |
| `(urushi-statusbar-text TEXT &rest PROPERTIES)` | TEXT as a segment. PROPERTIES go to its `TextBlock`. |
| `(urushi-statusbar-button TEXT ACTION &rest PROPERTIES)` | TEXT as a segment that calls ACTION when clicked. PROPERTIES go to its `Button`. |
| `(urushi-statusbar-window &optional FRAME)` | The window the bar is about. |

And the segments, each a function of the window and of properties for what it draws:

| Segment | What it says |
| --- | --- |
| `urushi-statusbar-message` | The first line of what Emacs is saying in the echo area, while it says it. Put it in FILL. |
| `urushi-statusbar-vc` | The branch the file is on, as version control says it. |
| `urushi-statusbar-buffer` | The name of the buffer, with a dot when it is not saved. |
| `urushi-statusbar-diagnostics` | How many errors and warnings Flymake has found. Clicking it lists them. |
| `urushi-statusbar-position` | The line and column of the point. |
| `urushi-statusbar-encoding` | How the file is encoded, and how its lines end. |
| `urushi-statusbar-major-mode` | The name of the major mode. A mode of CC Mode is named without the flags it adds, `C#` for `C#//l`. |

Properties given to a part replace the ones it has rather than repeating them.

## Template

A bar along the bottom, in the colours of the mode line, in a file on your `load-path`, say `lisp/my-statusbar.el`:

```elisp
;;; my-statusbar.el --- My status bar  -*- lexical-binding: t; -*-

(require 'urushi-screen)
(require 'urushi-statusbar)

(defun my-statusbar--color (attribute frame)
  (urushi-screen-color (face-attribute 'mode-line attribute frame t)))

(defun my-statusbar (frame)
  (urushi-statusbar frame
                   :left '(urushi-statusbar-vc
                           urushi-statusbar-buffer)
                   :right '(urushi-statusbar-diagnostics
                            urushi-statusbar-position
                            urushi-statusbar-encoding
                            urushi-statusbar-major-mode)
                   :Height 24
                   :Background (or (my-statusbar--color :background frame)
                                   "Transparent")
                   :Foreground (or (my-statusbar--color :foreground frame)
                                   "White")))

(provide 'my-statusbar)
```

And in your init file, below the windows:

```elisp
(when (featurep 'urushi)
  (require 'my-statusbar)
  (setq urushi-screen-components
        '(urushi-screen-windows
          my-statusbar)))
```

## Recipes

### Without the mode lines

The bar says what the mode lines say, and they can go. Without them, windows one above the other have nothing between them, so draw a line there instead: urushi draws the dividers of `window-divider-mode` as Emacs does.

```elisp
(setq-default mode-line-format nil)
(setq window-divider-default-places t
      window-divider-default-bottom-width 1
      window-divider-default-right-width 1)
(window-divider-mode 1)
```

The colour of the line is the foreground of the face `window-divider`.

A package that draws the mode line, such as doom-modeline, sets `mode-line-format` again whenever it is turned on. Turn it off first, and after it has been loaded: with a package manager that loads packages after the init file, that is in a hook that runs after them, such as `elpaca-after-init-hook`.

### Messages in the bar

What Emacs says in the echo area can be said in the bar instead, and the echo area left out of sight:

```elisp
(urushi-statusbar frame
                 :left '(urushi-statusbar-vc)
                 :fill '(urushi-statusbar-message)
                 :right '(urushi-statusbar-position))

(setq urushi-screen-echo-area 'when-active)
```

While the minibuffer is being typed in, Emacs puts a message after what is typed rather than in the echo area, where it wraps in a minibuffer of a fixed width and runs into the question being asked. Have the bar say those too:

```elisp
(add-hook 'set-message-functions #'urushi-statusbar-take-minibuffer-message)
```

They stay for `urushi-statusbar-minibuffer-message-timeout` seconds, or until the minibuffer is left.

With `urushi-screen-echo-area` set to `when-active`, the line at the bottom of the frame is shown only while the minibuffer is being typed in there, as `M-:` does; a minibuffer that floats in a child frame leaves it out of sight. Emacs keeps the line whether or not it is shown, so the frame is made taller than its room by that line, and the line goes under the bar.

### Segments of your own

A segment is any function of the window. The name of the project the window's buffer is in:

```elisp
(defun my-statusbar-project (window &rest properties)
  (when-let* ((project (with-selected-window window (project-current))))
    (apply #'urushi-statusbar-text (project-name project) properties)))
```

Something to click, with `urushi-statusbar-button`. The major mode, which describes itself when clicked:

```elisp
(defun my-statusbar-mode (window &rest properties)
  (let ((mode (buffer-local-value 'major-mode (window-buffer window))))
    (apply #'urushi-statusbar-button (symbol-name mode)
           (lambda () (describe-function mode))
           properties)))
```

List them like the others: `:left '(my-statusbar-project)`.

### The look of one segment

Give a segment properties where it is listed:

```elisp
:right '((urushi-statusbar-position :FontSize 12 :Margin "4,0,4,0")
         (urushi-statusbar-major-mode :Opacity 0.7))
```

### At the top, or anywhere else

The bar is a component, and goes wherever it is listed. Above the windows, under a title bar:

```elisp
(setq urushi-screen-components '(my-titlebar my-statusbar urushi-screen-windows))
```
