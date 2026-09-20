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

(defun urusi-site-start--show ()
  "Show the screen, once there is an Emacs to show."
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
