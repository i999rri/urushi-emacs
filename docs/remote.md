# Emacs in another process

The application loads Emacs into itself from `libemacs.dll`, and that is what it does unless it is told otherwise. It can instead start an Emacs of its own process and talk the [protocol](protocol.md) with it over that process's standard input and output, a message to a line. This is how the Emacs for Linux, built on the `host` window system and run in WSL, is shown in the window.

## Telling the application

Put the command line that starts Emacs in a file called `.urushi-emacs-remote` in your user folder (`%USERPROFILE%`), then start the application again. The first line of the file that is not empty and does not start with `#` is the command; the rest of the file is not read. With no such file, or no command in it, Emacs is the one in the application, as before.

It is a file and not an environment variable because the packaged application is given none of the environment it is started from.

For the Emacs that `scripts/linux/build-emacs.sh` builds, with the Lisp of this repository, where `/mnt/c/path/to/urushi-emacs` is where the repository is, seen from WSL:

```
# Emacs for Linux, in WSL
wsl.exe -e bash -lc "EMACS_HOST_PIPE=1 exec ~/dev/urushi/emacs-build/src/emacs -Q -L /mnt/c/path/to/urushi-emacs/lisp -l /mnt/c/path/to/urushi-emacs/lisp/urushi-site-start.el"
```

`EMACS_HOST_PIPE=1` is what has Emacs talk to a host over its standard input and output. The command is started in your user folder, with no console window.

## What is different

The log (`urushi-emacs.log`, beside the application) says which Emacs was chosen and with what command. What the process writes to its standard error goes to the log as Emacs's, and so does anything on its standard output that is not a message, a login shell's greeting for one. When the process exits, the log says its exit code.

An Emacs of its own process has no windows, so it is sent the keys, the pointer and the focus as the protocol's `key`, `pointer` and `focus`, what the input method settles on as `text`, and the size of its frame as `resize` once the window is laid out. It sends no `frame`: its frame is there once it has said `hello`.

A key is sent as the keyboard layout makes it: a character if it types one, with Ctrl and Alt as the modifiers `ctrl` and `meta` rather than part of the character, and the Windows key as `super`; otherwise the name Emacs has for it, `return`, `f5`, `kp-add`. Modifiers pressed alone, and the keys the input method takes, are not sent.

When Emacs exits, the application closes with it, as it does when Emacs is in the application. If it exits before it has said anything, the window stays, to show the log.
