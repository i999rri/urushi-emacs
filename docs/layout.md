# Layout

The window can be laid out in panels, the way an IDE lays out its editor, its file tree and its output: side by side, one above another, any size, hidden until needed. The Emacs frame is one panel among them, and what goes in the others is yours to say.

## How it works

A layout is a tree, written in Lisp, in the variable `urusi-layout`. It is put on the screen by the component `urusi-layout-component`, which takes the room the other components leave:

```elisp
(require 'urusi-layout)

(setq urusi-screen-components
      '(my-titlebar
        urusi-layout-component))
```

The tree is made of three things:

| Node | What it is |
| --- | --- |
| `(row PART...)` | Its parts side by side. |
| `(column PART...)` | Its parts one above another. |
| `(panel :content CONTENT)` | Something on the screen. |

Each part of a row or a column takes `:size` pixels (in the pixels XAML counts in), or shares what the parts with no size leave, in proportion to its `:weight`, which is 1 unless it says otherwise. A part with an `:id` can be shown, hidden and resized from Lisp.

What a panel holds is its `:content`:

| Content | What it is |
| --- | --- |
| `emacs` | The Emacs frame, windows and all. There is to be one. |
| `frame` | An Emacs frame of the panel's own, showing the buffer named by the panel's `:buffer` at first, or `*scratch*`. |
| `nil` | Nothing: a space. |
| a function | Called with the frame, returning a tree for `urusi-render`. |
| a tree | Put there as it is. |

A panel's own `:background`, `:padding`, `:margin` and `:corner-radius` are the panel's.

## Buffers in panels

A panel whose content is `frame` has an Emacs frame of its own, with windows that split and buffers that show as in any other. It is made the first time it is shown. Clicking in it selects it, and what is typed goes to it; clicking back in the other frame takes the keys back.

A buffer is sent to it with `urusi-layout-display-in-panel`, an action for `display-buffer-alist`. The panel it names is shown if it is hidden:

```elisp
(add-to-list 'display-buffer-alist
             '("\*compilation\*"
               (urusi-layout-display-in-panel)
               (panel . output)))
```

With that, `M-x compile` puts its output in the output panel, and the frame being typed in stays the one it was.

A frame of a panel is made what its panel needs by `urusi-layout-make-frame-functions`, which are called with the frame and the panel's `:id`. An output needs no tabs above it:

```elisp
(add-hook 'urusi-layout-make-frame-functions
          (lambda (frame _id)
            (let ((window (frame-root-window frame)))
              (set-window-parameter window 'tab-line-format 'none)
              (set-window-parameter window 'header-line-format 'none))))
```

## Changing it

| Command | What it does |
| --- | --- |
| `urusi-layout-hide` | Hides a part. The ones beside it take its room. |
| `urusi-layout-show` | Shows it again. |
| `urusi-layout-toggle` | Hides it if it is shown, shows it if not. |
| `urusi-layout-resize` | Gives it a size, or with none has it share. |
| `urusi-layout-reset` | Undoes every change. |

Between two parts that are shown is a splitter, which can be dragged. The part with a size of its own is the one that grows or shrinks, and the one that shares takes what is left; if neither has a size, the one before is given one.

What is changed is remembered apart from the layout as written: `urusi-layout` itself is never changed, and Emacs started again starts from it.

A row or a column whose parts are all hidden is hidden with them.

## Examples

Emacs, and nothing else, which is the layout there is to start with:

```elisp
(setq urusi-layout '(panel :id editor :content emacs))
```

An output under Emacs that shows itself when there is a compilation to show:

```elisp
(setq urusi-layout
      '(column
        (panel :id editor :content emacs)
        (panel :id output :size 220 :hidden t
               :content frame :buffer "*compilation*")))
```

A file tree on the left and an output under Emacs, the output hidden until it is wanted:

```elisp
(setq urusi-layout
      '(row
        (panel :id explorer :size 260 :background "#252526"
               :content my-file-tree)
        (column
         (panel :id editor :content emacs)
         (panel :id output :size 200 :hidden t
                :content my-output))))

(keymap-global-set "C-c o" (lambda () (interactive) (urusi-layout-toggle 'output)))
```

Emacs kept small in the top left corner, and the rest left to other things:

```elisp
(setq urusi-layout
      '(column
        (row (panel :id editor :size 640 :content emacs)
             (panel :content my-dashboard))
        (panel :weight 2 :content my-output)))
```

A space for the sake of a space, between Emacs and the edge of the window:

```elisp
(setq urusi-layout
      '(row (panel :size 48 :content nil)
            (panel :id editor :content emacs)
            (panel :size 48 :content nil)))
```
