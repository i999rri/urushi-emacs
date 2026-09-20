;;; site-start.el --- Bring urusi up around the user's init  -*- lexical-binding: t; -*-

;;; Commentary:

;; Installed as site-start.el, which Emacs loads before the init file.
;;
;; That is the only moment from which both halves of this work: urusi is
;; there to be configured by the time the init file runs, and the screen
;; is not shown until the init file has had its say about what it should
;; look like.
;;
;; It does nothing at all in an Emacs that is not inside the host, so the
;; same installation is an ordinary Emacs when run as one.

;;; Code:

;; Required below, and only in an Emacs that is inside the host.
(declare-function urusi--log "urusi" (format &rest arguments))
(declare-function urusi-start "urusi" ())
(declare-function urusi-screen-mode "urusi-screen" (&optional arg))
(defvar urusi-screen-mode)

(defcustom urusi-site-start-trace t
  "Whether to say what the init file is loading, as it loads it.

An init file is read before there is a screen to show anything on, and
before Emacs will answer anything, so one that does not finish leaves
nothing at all to go on.  Saying what is being loaded leaves the last
thing it reached, which is where it stopped."
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
worth having."
  (urusi--log "call-process %s %S" program arguments))

(defun urusi-site-start--trace (on)
  "Say what is being loaded and run while an init file is read, if ON."
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
  (unless urusi-screen-mode
    (condition-case error
        (progn
          (urusi--log "init %s read in %s, %d features"
                      (or user-init-file "(none)") (emacs-init-time)
                      (length features))
          (urusi-start)
          (urusi-screen-mode 1))
      (error (urusi--log "startup: %S" error)))))

(when (and (fboundp 'w32-host-available-p) (w32-host-available-p))
  (require 'urusi)
  (require 'urusi-screen)

  ;; How far Emacs got, for when it does not get all the way: nothing
  ;; Emacs writes to its standard error reaches the host, so the only
  ;; account of a startup is the one it sends.
  (urusi--log "site-start")

  ;; Whether anything Emacs runs comes back at all.  The host gives
  ;; Emacs a pipe for its output, and a child inherits what the parent
  ;; holds, so this is the first thing to know when one does not.
  (urusi--log "a child says %S"
              (condition-case error
                  (with-temp-buffer
                    (list (call-process "cmd" nil t nil "/c" "echo urusi-child")
                          (string-trim (buffer-string))))
                (error error)))

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
