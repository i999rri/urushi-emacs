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

**Today:** the table has a fourth field, `window`, the window Emacs makes its frames in on Windows, which frames of the host's own will not need. Messages to Emacs wait in a queue that Lisp looks at every 50 ms, so what is typed with the input method is 50 ms behind what is typed with keys.

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

**Today:** frames are windows of Emacs's that are on no screen, known by their window handles. The root frame's comes in the `frame` message, a panel frame's in the `Tag` of the element it is drawn in.

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

**Today:** `frame` has `window`, a window handle, and no `id`; only the root frame sends it, and `frame-deleted` is not sent. `caret` has no `frame` and is counted from the root frame's corner.

## The application to Emacs

| Type | Fields | Sent when |
| --- | --- | --- |
| `hello` | `version` (number), `host` (string), `scale` (number), `debug` (boolean) | Answering Emacs's `hello`. |
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
| `error` | `message` (string) | The application could not do what a message asked. |

`key`, `text`, `pointer`, `focus` and `resize` go to Emacs's C, which makes of them what the operating system's events would be to any other Emacs, in the order they came. The rest go to Lisp.

**Today:** keys, the pointer and the focus reach Emacs as Windows messages posted to its frame windows, not as messages. `text` is called `commit`. `resize` has no `frame` for the root frame.

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

## Host events

| Event | Fields | When |
| --- | --- | --- |
| `close` | | The window is being closed. It is not, until Emacs closes it: it asks about the buffers that are not saved first. |
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

A method the application does not have is answered with an `error`.
