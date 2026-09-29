;;; site-start.el --- Bring urushi up around the user's init  -*- lexical-binding: t; -*-

;;; Commentary:

;; Installed as site-start.el, which Emacs loads before the init file.
;;
;; That is the only moment from which both halves of this work: urushi is
;; there to be configured by the time the init file runs, and the screen
;; is not shown until the init file has had its say about what it should
;; look like.
;;
;; What the screen looks like is loaded only after the init file, so
;; that the init file can have its own: a urushi-screen.el of your own,
;; loaded from the init file or found first on `load-path', is the one
;; used, and the one that comes with urushi is used only when there is no
;; other.  The same goes for urushi-frame.el.
;;
;; It does nothing at all in an Emacs that is not inside the host, so the
;; same installation is an ordinary Emacs when run as one.

;;; Code:

;; Required below, and only in an Emacs that is inside the host.
(declare-function urushi--log "urushi" (format &rest arguments))
(declare-function urushi-start "urushi" ())
(declare-function urushi-screen-mode "urushi-screen" (&optional arg))
(declare-function urushi-frame-mode "urushi-frame" (&optional arg))
(defvar urushi-screen-mode)

(defcustom urushi-site-start-trace
  (file-exists-p (expand-file-name "urushi-trace" invocation-directory))
  "Whether to say what the init file is loading, as it loads it.

An init file is read before there is a screen to show anything on, and
before Emacs will answer anything, so one that does not finish leaves
nothing at all to go on.  Saying what is being loaded leaves the last
thing it reached, which is where it stopped.

It is on when a file named urushi-trace is beside Emacs.  This is read
before the init file is, so the init file cannot turn it on in time,
and nothing else reaches a packaged application as it starts."
  :type 'boolean
  :group 'urushi)

(defun urushi-site-start--loading (file &rest _)
  "Say that FILE is being loaded."
  (urushi--log "load %s" file))

(defun urushi-site-start--requiring (feature &rest _)
  "Say that FEATURE is being required.
`require' reaches the file itself from C, past any advice on `load',
so a feature that does not come back is a file that was never named."
  (urushi--log "require %s" feature))

(defun urushi-site-start--calling (program &rest arguments)
  "Say that PROGRAM is being run with ARGUMENTS, and waited for.
Emacs waits for what it runs this way, and a program that does not
finish is an Emacs that does not start, so what it was asked to do is
worth having.  So is which program it found by that name: the search
path is the environment's, and the environment is whoever started the
application's."
  (urushi--log "call-process %s (%s) %S"
              program (or (executable-find program) "not found") arguments))

(defun urushi-site-start--trace (on)
  "Say what is being loaded and run while an init file is read, if ON.

Also run a program and wait for it, because the host gives Emacs a
pipe for its output and a program inherits what the process holds: a
program that never comes back is the first thing to know about."
  (when on
    (urushi--log "exec-path %S" exec-path)
    (urushi--log "a child says %S"
                (condition-case error
                    (with-temp-buffer
                      (list (call-process "cmd" nil t nil "/c" "echo urushi-child")
                            (string-trim (buffer-string))))
                  (error error))))

  (dolist (advice '((load . urushi-site-start--loading)
                    (require . urushi-site-start--requiring)
                    (call-process . urushi-site-start--calling)
                    (call-process-region . urushi-site-start--calling)
                    (process-file . urushi-site-start--calling)))
    (if on
        (advice-add (car advice) :before (cdr advice))
      (advice-remove (car advice) (cdr advice)))))

(defun urushi-site-start--show ()
  "Show the screen, once there is an Emacs to show."
  (urushi-site-start--trace nil)
  (unless (bound-and-true-p urushi-screen-mode)
    (condition-case error
        (progn
          (urushi--log "init %s read in %s, %d features"
                      (or user-init-file "(none)") (emacs-init-time)
                      (length features))
          ;; Whichever the init file loaded, or the first on the path.
          (require 'urushi-screen)
          (require 'urushi-frame)
          (urushi--log "screen from %s" (symbol-file 'urushi-screen-mode))
          (urushi-start)
          (urushi-screen-mode 1)
          (urushi-frame-mode 1))
      (error (urushi--log "startup: %S" error)))))

(when (and (fboundp 'host-available-p) (host-available-p))
  (require 'urushi)

  ;; How far Emacs got, for when it does not get all the way: the
  ;; screen is not there yet to say anything on, so the only account of
  ;; a startup is the one it sends.
  (urushi--log "site-start")

  ;; This window system has no dialog box of Emacs's own, and Emacs
  ;; puts what it asks in one whenever the asking began with the mouse:
  ;; closing a window, clicking a tab shut.  With nothing to show it in,
  ;; the question was never asked and never answered, which read as
  ;; answering no.  Asked in the echo area it is asked.
  ;;
  ;; Set here rather than left to the init file, since it says what this
  ;; display can do and not what anyone prefers.  A host that is asked
  ;; to show the dialog itself would take this away again.
  (setq use-dialog-box nil)

  (when urushi-site-start-trace
    (urushi-site-start--trace t))

  ;; Last on the hook, so that whatever the init file put there has run
  ;; before the screen is built from what it decided.
  (add-hook 'after-init-hook #'urushi-site-start--show 90)

  ;; And again once Emacs falls idle, because `run-hooks' does not catch
  ;; what a hook function signals: one that fails takes every function
  ;; after it with it, and being last on the hook means being the first
  ;; to be lost that way.  Whichever of the two arrives first shows the
  ;; screen and the other finds it shown.
  (run-with-idle-timer 0.2 nil #'urushi-site-start--show))

;;; site-start.el ends here
