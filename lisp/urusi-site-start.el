;;; site-start.el --- Bring urusi up around the user's init  -*- lexical-binding: t; -*-

;;; Commentary:

;; Installed as site-start.el, which Emacs loads before the init file.
;;
;; That is the only moment from which both halves of this work: urusi is
;; there to be configured by the time the init file runs, and the screen
;; is not shown until the init file has had its say about what it should
;; look like.
;;
;; What the screen looks like is loaded only after the init file, so
;; that the init file can have its own: a urusi-screen.el of your own,
;; loaded from the init file or found first on `load-path', is the one
;; used, and the one that comes with urusi is used only when there is no
;; other.  The same goes for urusi-frame.el.
;;
;; It does nothing at all in an Emacs that is not inside the host, so the
;; same installation is an ordinary Emacs when run as one.

;;; Code:

;; Required below, and only in an Emacs that is inside the host.
(declare-function urusi--log "urusi" (format &rest arguments))
(declare-function urusi-start "urusi" ())
(declare-function urusi-screen-mode "urusi-screen" (&optional arg))
(declare-function urusi-frame-mode "urusi-frame" (&optional arg))
(defvar urusi-screen-mode)

(defcustom urusi-site-start-trace
  (file-exists-p (expand-file-name "urusi-trace" invocation-directory))
  "Whether to say what the init file is loading, as it loads it.

An init file is read before there is a screen to show anything on, and
before Emacs will answer anything, so one that does not finish leaves
nothing at all to go on.  Saying what is being loaded leaves the last
thing it reached, which is where it stopped.

It is on when a file named urusi-trace is beside Emacs.  This is read
before the init file is, so the init file cannot turn it on in time,
and nothing else reaches a packaged application as it starts."
  :type 'boolean
  :group 'urusi)

(defun urusi-site-start--loading (file &rest _)
  "Say that FILE is being loaded."
  (urusi--log "load %s" file))

(defun urusi-site-start--requiring (feature &rest _)
  "Say that FEATURE is being required.
`require' reaches the file itself from C, past any advice on `load',
so a feature that does not come back is a file that was never named."
  (urusi--log "require %s" feature))

(defun urusi-site-start--calling (program &rest arguments)
  "Say that PROGRAM is being run with ARGUMENTS, and waited for.
Emacs waits for what it runs this way, and a program that does not
finish is an Emacs that does not start, so what it was asked to do is
worth having.  So is which program it found by that name: the search
path is the environment's, and the environment is whoever started the
application's."
  (urusi--log "call-process %s (%s) %S"
              program (or (executable-find program) "not found") arguments))

(defun urusi-site-start--trace (on)
  "Say what is being loaded and run while an init file is read, if ON.

Also run a program and wait for it, because the host gives Emacs a
pipe for its output and a program inherits what the process holds: a
program that never comes back is the first thing to know about."
  (when on
    (urusi--log "exec-path %S" exec-path)
    (urusi--log "a child says %S"
                (condition-case error
                    (with-temp-buffer
                      (list (call-process "cmd" nil t nil "/c" "echo urusi-child")
                            (string-trim (buffer-string))))
                  (error error))))

  (dolist (advice '((load . urusi-site-start--loading)
                    (require . urusi-site-start--requiring)
                    (call-process . urusi-site-start--calling)
                    (call-process-region . urusi-site-start--calling)
                    (process-file . urusi-site-start--calling)))
    (if on
        (advice-add (car advice) :before (cdr advice))
      (advice-remove (car advice) (cdr advice)))))

(defun urusi-site-start--show ()
  "Show the screen, once there is an Emacs to show."
  (urusi-site-start--trace nil)
  (unless (bound-and-true-p urusi-screen-mode)
    (condition-case error
        (progn
          (urusi--log "init %s read in %s, %d features"
                      (or user-init-file "(none)") (emacs-init-time)
                      (length features))
          ;; Whichever the init file loaded, or the first on the path.
          (require 'urusi-screen)
          (require 'urusi-frame)
          (urusi--log "screen from %s" (symbol-file 'urusi-screen-mode))
          (urusi-start)
          (urusi-screen-mode 1)
          (urusi-frame-mode 1))
      (error (urusi--log "startup: %S" error)))))

(when (and (fboundp 'host-available-p) (host-available-p))
  (require 'urusi)

  ;; How far Emacs got, for when it does not get all the way: the
  ;; screen is not there yet to say anything on, so the only account of
  ;; a startup is the one it sends.
  (urusi--log "site-start")

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

  (when urusi-site-start-trace
    (urusi-site-start--trace t))

  ;; Last on the hook, so that whatever the init file put there has run
  ;; before the screen is built from what it decided.
  (add-hook 'after-init-hook #'urusi-site-start--show 90)

  ;; And again once Emacs falls idle, because `run-hooks' does not catch
  ;; what a hook function signals: one that fails takes every function
  ;; after it with it, and being last on the hook means being the first
  ;; to be lost that way.  Whichever of the two arrives first shows the
  ;; screen and the other finds it shown.
  (run-with-idle-timer 0.2 nil #'urusi-site-start--show))

;;; site-start.el ends here
