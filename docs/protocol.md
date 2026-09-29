# Protocol

Emacs and the application it runs in talk in messages: one JSON object each, UTF-8, with a `"type"` that says what it is. Nothing in a message is particular to one operating system, so that the application on Windows, on macOS and on Linux is the same thing to Emacs, and Emacs the same thing to each of them.

This page is the protocol as it is to be, version 2. Where version 1, what runs today on Windows, does something else, it says so under **Today**.

## The connection

The application loads Emacs as a library, and gives it a table of three things when Emacs asks for it as it starts:

| Field | What it is |
| --- | --- |
| `version` | The version of the table, which Emacs checks before it uses the rest. |
| `post (message)` | Emacs to the application. Called on Emacs's thread; the application is to take the message to its own thread before it does anything with it. |
| `on_event (fn, data)` | Called once by Emacs, with the function the application calls with each of its messages, from any thread. Emacs keeps them until it reads them. |

A message belongs to whoever sends it and is not kept by whoever gets it. Only C types cross, because the application may be built with another compiler and another runtime than Emacs.

The table is `host_api`, in `libemacs/src/host.h` of Emacs, and the application exports `host_get_api` for Emacs to find it by.

The application can instead start Emacs as a process of its own, which then sends each message as a line on its standard output and reads the application's as lines from its standard input: see [Emacs in another process](remote.md).

Messages the application sends go into one of two queues. Keys, text, the pointer and the focus are input, and Emacs's C reads them as any other Emacs reads the events of its window system. The rest wait for Lisp, and Emacs is told that they are there: the application's message wakes Emacs, which is already waiting for it as it waits for a key, and the command loop calls `host-message-function`. Lisp also looks on a timer every thirty seconds, for a waking that never arrived; before there was any telling it was the only way, and it looked every 50 ms.

How Emacs is woken is the system's to decide. Everywhere but Windows it is a pipe of Emacs's own, waited on beside the keyboard; on Windows it is the message queue every Emacs there already has, a pipe being something `sys_select` would pass over.

## Units

Two kinds of length go back and forth, and every length says which it is by where it is:

| Unit | Used for |
| --- | --- |
| Pixels | Anything Emacs lays out or is told about: the size of a frame, where the pointer is in it, the cursor. They are the pixels of the screen. |
| Layout units | Anything in the tree Lisp builds for the application to draw: sizes in the layout, splitters, fonts. They are the application's own, 96 to the inch on Windows, points on macOS. |

`scale` in the application's `hello` is how many pixels there are to a layout unit.

## Frames

A frame is known by an `id`, a string Emacs gives it. The first frame is the root frame; the others are shown inside it, in a panel or floating over it, where Lisp puts them.

Emacs has no window of its own for a frame: the application draws each frame out of what Emacs laid out, and Emacs does not draw.

**Today:** no `frame` message is sent. A frame is first heard of in the `begin` of what it draws, under the name Emacs makes of it — the hash of the frame, printed in hexadecimal, which Lisp makes the same name from — and the element it is drawn on is named `urusi-emacs:` and that name.

## Starting

1. Emacs sends `hello`.
2. The application answers with `hello`.
3. Emacs sends `frame` for each frame it makes, the root frame first, and the application sends `resize` for each once it knows how much room there is.

A side that gets a `hello` whose `version` it does not speak says so with `error` and sends nothing else.

## Emacs to the application

| Type | Fields | Sent when |
| --- | --- | --- |
| `hello` | `version` (number) | Emacs is ready to be talked to. |
| `frame` | `id` (string), `parent` (string, absent for the root frame) | A frame has been made. |
| `frame-deleted` | `id` | A frame is gone. |
| `screen` | `xaml` (string, when it changed), `events` (array, with `xaml`), `rows` (array) | The screen has changed. See [Screen](#screen). |
| `caret` | `frame` (string), `x`, `y`, `width`, `height` (pixels, from the frame's corner) | The cursor has moved, for the input method to put its candidates beside it. |
| `measure` | `family` (string), `size` (layout units) | Lisp needs to know how wide a character of a font is drawn. Answered by `measured`. |
| `call` | `id` (number), `method` (string), `args` (object) | Lisp asks the application to do something. Answered by `reply`. See [Calls](#calls). |
| `log` | `text` (string) | A line for the application's log. |
| `debug` | `on` (boolean) | `urusi-debug-mode` has been turned on or off. |
| `draw` | `op` (string), and what that op has to say | Redisplay drew something. One line for each, in the order they were drawn. See [Drawing](#drawing). |
| `picture` | `frame` (string), `width`, `height` (pixels), `moved` (array), `drawn` (array) | The pixels redisplay drew, for an application that does not draw them itself. See [Drawing](#drawing). |
| `font` | `id` (number), and either which file it is or the file | A font that something to be drawn is in. See [Fonts](#fonts). |
| `image` | `id` (number), `width`, `height` (pixels), `pixels` (base64) | The pixels of an image about to be drawn. See [Images](#images). |
| `image-gone` | `id` (number) | An image Emacs has let go of, whose number may be given to another. |
| `pointer` | `shape` (string) | The shape the pointer is to take over the frame, when it changes. One of `none`, `arrow`, `text`, `hand`, `busy`, `horizontal-drag`, `vertical-drag`, and the eight edges and corners: `left-edge`, `top-left-corner`, `top-edge`, `top-right-corner`, `right-edge`, `bottom-right-corner`, `bottom-edge`, `bottom-left-corner`. |

`pointer` is the one name used for two unrelated things: this one says what the pointer is to look like, and the application's says what the pointer did. Which is which is decided by who sent it.

**Today:** neither `frame` nor `frame-deleted` is sent. `caret` has no `frame` and is counted from the root frame's corner. `hello` carries `window-system` as well, the name of what draws Emacs's frames, which the application reads to refuse an Emacs that draws its own.

## The application to Emacs

| Type | Fields | Sent when |
| --- | --- | --- |
| `hello` | `version` (number), `host` (string), `scale` (number), `draws` (boolean), `debug` (boolean) | Answering Emacs's `hello`. `draws` is whether it draws what Emacs says to draw; see [Drawing](#drawing). |
| `resize` | `frame` (string), `width`, `height` (pixels) | The room a frame has, when it changes. |
| `key` | `frame` (string), see [Keys](#keys) | A key was pressed or let go. |
| `text` | `frame` (string), `text` (string) | The input method settled on text. |
| `composition` | `frame` (string), `text` (string) | What the input method is still turning over, to be drawn at the cursor; `""` once there is nothing. |
| `pointer` | `frame` (string), see [The pointer](#the-pointer) | The pointer did something over a frame. |
| `focus` | `focused` (boolean) | The application's window has come to the front, or gone behind. |
| `event` | `id` (string), `args` (object) | Something happened to an element Lisp built. |
| `host-event` | `event` (string), and what the event has to say | Something happened to the window that Lisp did not ask for. See [Host events](#host-events). |
| `measured` | `family`, `size`, `narrow`, `wide` (layout units) | Answering `measure`: how wide `0` and `あ` are drawn. |
| `reply` | `id` (number), `value`, or `error` (string) | Answering `call`. |
| `stale` | | The application does not have what Emacs thinks it has on the screen, and wants all of it again. |
| `redraw` | | The application has come by something it had not when the screen was drawn, and wants everything drawn again. |
| `want-font` | `id` (number) | The application has something to draw from a font and has not the file it is in. |
| `want-image` | `id` (number) | The application has an image to draw and has not its pixels. |
| `error` | `message` (string) | The application could not do what a message asked. |

`key`, `text`, `pointer` and `focus` go to Emacs's C, which makes of them what the operating system's events would be to any other Emacs, in the order they came. Everything else waits for Lisp, which takes it with `host-take-events`. The two are separate queues, each holding 4096 messages, the oldest dropped when one is full: what is newest is what is still worth acting on.

`composition` is Lisp's on purpose, although it comes from the input method as `text` does: what the input method is still turning over is drawn by Lisp at the cursor, and is no input until it comes as `text`.

**Today:** `resize` is Lisp's as well, and is to become C's.

**Today:** `resize` has no `frame` for the root frame.

## Keys

| Field | What it is |
| --- | --- |
| `down` | Whether the key was pressed (`true`) or let go. |
| `char` | The character the key types with the modifiers other than `ctrl` and `meta` taken into account, as the keyboard layout says, if it types one. |
| `name` | Otherwise, the name of the key as Emacs calls it: `return`, `f5`, `left`, `kp-add`. |
| `modifiers` | Which of `ctrl`, `meta`, `shift`, `super`, `hyper` and `alt` are held. Which key of the keyboard is `meta` is the application's to say, the Alt key on Windows and Option on macOS by default. |
| `repeat` | Whether the key is held and repeating. |

A key the input method takes is not sent: what it makes of the keys comes as `composition` and `text`.

## The pointer

| Field | What it is |
| --- | --- |
| `kind` | `down`, `up`, `move`, `wheel` or `leave`. |
| `button` | For `down` and `up`: 1 the left button, 2 the middle, 3 the right, 4 and 5 back and forward. |
| `x`, `y` | Where the pointer is, in pixels from the frame's corner. |
| `clicks` | For `down`: how many presses there have been in a row, 2 for a double click, as the operating system counts them. |
| `dx`, `dy` | For `wheel`: how far, in lines, positive away from the person and to the right. |
| `modifiers` | As for keys. |

## Screen

What Lisp built for the application to draw. The XAML around the rows comes only when it has changed; the rows come each time, each with a `key`, and with XAML of its own only when it is new or has changed.

| Field | What it is |
| --- | --- |
| `xaml` | The XAML around the rows. |
| `events` | The events Lisp wants of elements in it: `name`, the element's name; `event`, `Click`, `TextChanged`, `SelectionChanged` or `Toggled`; `id`, what to send back in `event`. |
| `rows` | For each panel of rows: `panel`, its name, and `items`, its rows in order, each with `key`, and `xaml` and `events` when it has them. |

A row that comes with only its key is the one the application already has. If it does not have it, it sends `stale`.

XAML is the application's on Windows; on macOS and Linux the tree is the same and is written in what the application there reads. The tree itself, what Lisp builds it out of, is on [Layout](layout.md) and the pages of each component.

## Drawing

The screen is the chrome Lisp built; this is what is inside it, the text of the buffers and everything redisplay puts there.

An application that can draw says so in its `hello`, with `draws`. Emacs then says what it drew, and the application draws it. One that cannot is handed the pixels instead, in `picture` messages, and everything below about drawing is not sent to it.

A `picture` is the frame as it now stands, said as what changed in it: `moved`, the boxes of pixels that were already on the screen and have been shifted (`x`, `y`, `width`, `height`, `toY`), which is what scrolling comes to; and `drawn`, the boxes that were drawn again (`x`, `y`, `width`, `height`, `cells`, base64, a row at a time). The moves are done before the boxes are drawn.

Saying it rather than drawing it is the cheaper of the two by a long way — a screen of text is a few hundred things to do against the megabyte its pixels come to — and the text is then drawn by the same hand that draws the text around it, so it looks like the rest of the application rather than like a picture of Emacs.

### The commands

Each command is a line of its own, `{"type":"draw", "op": ...}`. A screen's worth comes between a `begin` and an `end`, and nothing is shown until the `end` arrives: a screen drawn halfway is a screen no one meant.

Emacs writes a screen's lines in one go, so that a screen is one write and not a few hundred. How they then arrive is the transport's doing: down a pipe they come back a line at a time, and in the same process they arrive as they were written, every line in the one message. The application is to read either.

Order is the whole of it. The text goes over the background that was filled before it, so an application that read them in any other order would show something else.

| Op | Fields | What it is |
| --- | --- | --- |
| `begin` | `frame` (string), `width`, `height` (pixels) | A screen of this frame begins, and it is this big. |
| `fill` | `x`, `y`, `width`, `height`, `color` | A box filled. |
| `rectangle` | `x`, `y`, `width`, `height`, `color` | A box drawn around, a pixel wide. |
| `line` | `x0`, `y0`, `x1`, `y1`, `color` | A line from one corner to the other. |
| `copy` | `x`, `y`, `width`, `height`, `toY` | Pixels already on the screen, moved: what a window scrolling comes to. |
| `clip` | `x`, `y`, `width`, `height` | Nothing outside this box is drawn until `unclip`. |
| `unclip` | | |
| `glyphs` | `font` (number), `size` (pixels), `y` (the baseline), `color`, `ids` (array), `xs` (array) | A run of glyphs of one font. See [Fonts](#fonts). |
| `image` | `image` (number), `x`, `y`, `width`, `height`, `imageWidth`, `imageHeight`, `matrix` (six numbers), `smooth` (boolean) | An image, drawn within the box. See [Images](#images). |
| `end` | `frame` (string) | The screen is whole. |

A colour is `#rrggbb`. Everything else is in pixels, from the frame's corner.

Emacs draws only what changed, so what a screen does not say is what is already there. That is why `copy` is a command and not a redrawing: the pixels of a scroll are on the screen already, and moving them is cheaper than saying them again. It also means these commands are not to be replayed — a `copy` done twice scrolls twice.

### Fonts

A glyph is numbered by the file it is in and nothing else. Emacs reads the font files itself, so what it says to draw is a glyph of a file rather than a character of a font: a font found by name at the application's end would be another file, numbering them otherwise. So the file itself is what goes over.

Emacs says which file a font is the first time it draws in it, and sends the file when it is asked for:

| Message | Fields |
| --- | --- |
| `font`, which file | `id`, `instance` (which font of the file), `file` (its path), `size` (bytes), `when` (when it was last written) |
| `want-font` | `id` |
| `font`, the file | `id`, `bytes` (base64) |

The application keeps a file it has been sent, under what the first of these says of it, so that a later run asks for none of them. Two files of the same path, size and time are the same file; one that has changed since is another, and comes again under another number.

A file is tens of megabytes, and the screen goes on being drawn while it is asked for and sent: what is in that font is left undrawn until the application has it, and the application says `redraw` once it has, for what was left out to be said again. Nothing is to wait on a font.

### Images

Emacs decodes an image itself, whatever format it was in, and the application draws the pixels. It cannot be the other way around: an image may never have been a file at all — an SVG Lisp wrote, or the data of a `create-image` — and Emacs is the one that knows how to read all of them.

The pixels go over once, as the image is first said, and the `image` op names that number for every screen that draws it. They are sent as the application draws them and not as Emacs holds them: four bytes to a pixel, blue first, and already multiplied by the alpha that Emacs's mask decided. Emacs is the one that knows what a mask means, so the application is left with pixels and nothing to decide.

`want-image` is for an application that has an image to draw and has not its pixels, which should not happen in the ordinary way of things: it is there for one that lost them, as after a graphics device is lost.

`image-gone` says Emacs has let go of an image, so the application can let go of what it made from the pixels. The number may be given to another image afterwards.

An image is drawn by its matrix, not by its box. `imageWidth` and `imageHeight` are its size as the pixels came; `matrix` carries that image from its own corner to where this row's part of it goes, scaled and turned as the image asked to be, as `a b c d dx dy`, where a point x,y of the image goes to `a·x + c·y + dx`, `b·x + d·y + dy`. So the application applies the matrix, draws the image at its own size, and leaves out whatever falls outside the box. `smooth` says whether to blend the pixels, which Emacs asks for when the image is drawn smaller than it is and not when it is drawn larger.

A tall image is drawn a row at a time, and every row says the same image with a matrix that brings a different part of it into that row's box. Nothing else says which part: the box and the matrix are the whole of it.

Emacs works all of this out because it is the one that knows what the image was asked to be — `:scale`, `:width`, `:rotation` and the rest are its to read — and because an application told the matrix has nothing left to decide.

## Host events

| Event | Fields | When |
| --- | --- | --- |
| `close` | | The window is being closed. It is not, until Emacs closes it: it asks about the buffers that are not saved first. The application waits five seconds for Emacs to say something and closes anyway if it does not. |
| `state` | `state`: `normal`, `maximized`, `minimized` or `fullscreen` | The window was maximized, restored, and so on, other than by Lisp. |
| `theme` | `dark` (boolean) | The operating system switched between light and dark. |
| `splitter` | `name`, `before`, `after` (layout units) | A splitter was let go, with the sizes of the parts either side of it. |

**Today:** `activated` and `deactivated` are sent too, and nothing reads them; `focus` takes their place.

## Calls

| Method | Args | Reply |
| --- | --- | --- |
| `window.title` | `title` | `null` |
| `window.state` | `state`: `normal`, `maximized`, `minimized` or `fullscreen` | `null` |
| `window.topmost` | `on` (boolean) | `null` |
| `window.size` | | `width`, `height`: the window's size, in pixels |
| `window.resize` | `width`, `height` (pixels) | `null` |
| `window.theme` | | `dark` (boolean) |
| `dialog.open-file` | | The file chosen, or `null` |
| `dialog.ask` | `message`, and `title`, `accept`, `other`, `cancel` for the ones offered | `accept`, `other` or `cancel`, whichever was chosen |

A method the application does not have is answered with an `error`.

`dialog.ask` is answered when the person answers the dialog, which may be long after it was asked; Emacs goes on in the meantime, and whatever is to happen after the answer happens in the reply. Only one dialog is up at a time: a second asked for while one is up is answered with an `error`, since a dialog laid under another is one nobody can answer.
