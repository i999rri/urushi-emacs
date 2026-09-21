# Child frames

A child frame is a frame that floats over another one: a minibuffer that opens in the middle of the frame, a completion popup by the point, a box of documentation. urusi draws each where Emacs put it, over the frame it belongs to, and what it looks like is yours to change.

## How it works

Where a child frame goes and how big it is are Emacs's to decide, and urusi puts it there. What is put there is up to `urusi-screen-child-frame-function`, which takes the frame and returns it as XAML, as big as the frame is.

The one urusi starts with is `urusi-screen-child-frame-body`, which draws it the way Emacs would: its background, the border around it in the face `child-frame-border`, and its windows. A function of your own can wrap that in anything XAML has, which is how a child frame gets a shadow or rounded corners.

## A shadow and rounded corners

```elisp
(defun my-child-frame (frame)
  `(Border :CornerRadius 8
           :Translation "0,0,32"
           (Border.Shadow (ThemeShadow))
           ,(urusi-screen-child-frame-body frame)))

(setq urusi-screen-child-frame-function #'my-child-frame)
```

`ThemeShadow` is the shadow Windows draws under its menus and flyouts. How far the frame seems to float, and so how wide and soft the shadow is, is the third number of `Translation`. On a dark background it is faint; that is how Windows draws it there too.

`CornerRadius` rounds the corners, and cuts off what is drawn in them.

## For some child frames and not others

The frame is there to look at. A minibuffer that floats, as `mini-frame` opens, is a frame whose `minibuffer` parameter is `only`; a completion popup, as `corfu` opens, shows a buffer of its own:

```elisp
(defun my-child-frame (frame)
  (let ((body (urusi-screen-child-frame-body frame))
        (buffer (buffer-name (window-buffer (frame-root-window frame)))))
    (cond
     ;; The minibuffer: floating well above the frame.
     ((eq (frame-parameter frame 'minibuffer) 'only)
      `(Border :CornerRadius 8 :Translation "0,0,32"
               (Border.Shadow (ThemeShadow))
               ,body))
     ;; Completion: a smaller shadow, and square corners.
     ((equal buffer " *corfu*")
      `(Border :Translation "0,0,16"
               (Border.Shadow (ThemeShadow))
               ,body))
     (t body))))
```

Anything else a frame says about itself will do as well: its name, its other parameters, the buffer in it.
