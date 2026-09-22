# Design: frames of the host's own

Status: proposed. This is how Emacs is to have frames that the application draws, on every operating system alike, in place of the Windows frames it borrows today. The messages it takes are on [Protocol](../protocol.md).

## What is wrong with borrowing

On Windows, Emacs makes its frames as it always does, as windows, and they are put on no screen: the application draws them from what Emacs laid out, and posts to their windows the keys and the pointer as Windows would. It works, and it is Windows's alone:

- Emacs goes on treating the windows as its own. It raised one to the front when a buffer was shown in it, and gave another the focus when it was clicked, and the keys stopped reaching the application.
- Keys and the input method come by two ways, Windows messages and the protocol, with nothing to keep them in order.
- The Emacs of macOS and the Emacs of Linux have no such windows to borrow, and each would have to be borrowed from again, differently.

## What is proposed

A window system of its own, `host`, beside `w32`, `ns`, `pgtk` and the rest: frames with no window at all, laid out by Emacs and drawn by the application, which Emacs is told everything about in the protocol and tells everything to in it.

| Part | What it does |
| --- | --- |
| `hostterm.c` | The terminal: its redisplay interface, which draws nothing and says what changed; reading the queue of the application's messages into Emacs's events, in C, as each window system reads its own; the focus and the frame each key goes to. |
| `hostfns.c` | Frames: making one (`make-frame` on the `host` window system), its parameters, its size as `resize` says, deleting it, and saying so to the application (`frame`, `frame-deleted`). |
| `hostfont.c` | Fonts, from the font engine Emacs has for Android (`sfnt.c`, `sfntfont.c`), which reads the font files itself and needs nothing of the system. It draws nothing here: it only measures. |
| `hostscreen.c` | What there is today: the rows and the cursor of a window, read back for the application. |
| `host.h` | The table the application gives Emacs, as `w32host.h` is today, without the window. |

### One window system to a build

Emacs is written for one window system to a build: much of it takes the display of any frame to be of the one type the build has (`Display_Info`), images and faces most of all. So `host` is not built beside `w32` or `ns` but in place of them, as Android and Haiku are in place of X:

| System | Build |
| --- | --- |
| Linux | `--with-host`, without X or GTK. |
| macOS | `--with-host`, without `ns`. |
| Windows | `--with-host`, and without the Windows GUI, which MinGW builds always have today: `configure.ac` and the files of the GUI (`w32fns.c`, `w32term.c`, `w32menu.c`, `w32font.c` and the rest) are to be left out, while what makes Emacs run on Windows at all (`w32.c`, `w32proc.c`) stays. |

Building `host` beside the system's own was weighed and left: `Display_Info` would have to be two types at once, and every place that assumes one would have to learn which.

### Windows of the application, and Emacs without any

Each root frame is a window of the application's: `frame` without a `parent` opens one, and `frame-deleted` closes it. Closing the window deletes its frame, as `C-x 5 0` would, and not Emacs; the last one closed leaves Emacs running with no frames, as `emacs --daemon` does, until a frame is made again, by `make-frame` or by `emacsclient -c`, and a window opens for it. Leaving Emacs is `C-x C-c`, or asking the application to quit.

Today the application is one window with one root frame, and closing it is leaving Emacs.

### Fonts

The application draws the text in its own fonts; Emacs only has to lay it out in the same widths. `sfntfont.c` is written to be given to a platform (`init_sfntfont_vendor`), with the platform drawing the glyphs; here nothing draws, and the fonts are the files the application says to look in (`C:\Windows\Fonts`, `/System/Library/Fonts`, the directories of fontconfig). What the application's drawing and Emacs's measuring still differ by is corrected as it is today, with `measure`.

### Input

`key`, `text`, `composition`, `pointer`, `focus` and `resize` are read in C, from the queue the application fills, and made into the events any other window system makes: a key into a keystroke with its modifiers, the pointer into a click with its count, the focus into a focus event. They are read in the order they came, keys and text alike, and as soon as Emacs is waiting for input rather than when a timer comes round.

## Order of work

1. The protocol, version 2, written down ([Protocol](../protocol.md)), and the table renamed from `w32_host_api` to `host_api` with the old names kept for now.
2. A host for tests: the protocol spoken by a program with no window, which starts Emacs, gives it keys and a size, and reads back its screen. It is what everything after is tested with, on any system.
3. `host` on Linux, where there is no Windows GUI to take out first: `--with-host`, frames, input, fonts.
4. `host` on Windows: the GUI taken out of the MinGW build, and the application moved to it from the borrowed windows.
5. The applications for macOS and Linux.

## Seeing that it works

The work is done on Windows, and `host` comes first on Linux, in WSL:

| What | How |
| --- | --- |
| Building | The fork is built in WSL, `--with-host` and without X, from the same tree. |
| Tests | Emacs is started with the protocol on a pipe instead of the table, and the host for tests sends it `hello`, `resize`, keys and the pointer, and reads the rows of the `screen` it sends back. No window is needed. |
| Using it | The application on Windows speaks the protocol over a pipe or a socket as well as to the Emacs it loads, and draws the Emacs in WSL: the frames of `host` seen, typed in and clicked, before there is an application for Linux. |
| The Linux application | Once there is one, it runs in WSL and is shown on Windows by WSLg, which is to be turned on for that (`guiApplications` in `.wslconfig`). |

## Questions still open

- Whether the Windows GUI can be taken out of a MinGW build without a great deal else coming with it. Cygwin builds without it, which is a start.
- Menus and dialogs: Emacs's own (`x-popup-menu`, `x-popup-dialog`) are to be the application's to show, through `call`, and file dialogs likewise.
- Images: Emacs decodes them to draw them, and here the application would draw them; whether Emacs needs to decode them at all, or only to know their size.
- The clipboard and selections, which each window system has its own way to.
