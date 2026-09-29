;;; urushi-debug.el --- An account of what happened, in the host's log  -*- lexical-binding: t; -*-

;;; Commentary:

;; When the screen stops making sense, what Emacs was doing cannot be
;; asked of it: the echo area and *Messages* are drawn by the same
;; screen, and the keys that would ask may be going somewhere else.
;; `urushi-debug-mode' writes it down as it happens instead, in the log
;; the host keeps beside it, which can be read from outside:
;;
;;   - each command, with the frame and window it ran in, and how deep
;;     in the minibuffer it was;
;;   - each change of focus between frames, and where the keys go;
;;   - every message, including the ones that go by too fast to read;
;;   - every error a command signals;
;;   - on the host's side, each key it passes on, and what the input
;;     method says.
;;
;; It is off unless asked for, because a day of it is a large file.  It
;; has to be on before things go wrong, since once they have, the keys
;; to turn it on may not arrive: so the host turns it on from the start
;; when it is run under a debugger, as it is from Visual Studio.

;;; Code:

(require 'urushi)

(defgroup urushi-debug nil
  "An account of what happened, in the host's log."
  :group 'urushi)

(defun urushi-debug--frame (frame)
  "Return a short name for FRAME that tells frames apart in the log."
  (if (frame-live-p frame)
      (format "%s%s"
              (or (frame-parameter frame 'urushi-panel)
                  (and (frame-parent frame) "child")
                  "root")
              (if (eq frame (selected-frame)) "*" ""))
    "dead"))

(defun urushi-debug--where ()
  "Return where Emacs is now: frame, window, buffer and minibuffer depth."
  (let ((frame (selected-frame)))
    (format "frame %s (keys to %s), buffer %s, minibuffer %d%s"
            (urushi-debug--frame frame)
            (urushi-debug--frame (or (frame-focus frame) frame))
            (buffer-name (window-buffer (selected-window)))
            (minibuffer-depth)
            (if-let* ((window (active-minibuffer-window)))
                (format " in %s" (urushi-debug--frame (window-frame window)))
              ""))))

(defun urushi-debug--command ()
  "Write down the command that just ran, and where it left Emacs."
  (urushi--log "command %S: %s" this-command (urushi-debug--where)))

(defun urushi-debug--focus ()
  "Write down which frames have the focus now."
  (urushi--log "focus %s: %s"
              (mapconcat (lambda (frame)
                           (format "%s=%S" (urushi-debug--frame frame)
                                   (frame-focus-state frame)))
                         (frame-list) " ")
              (urushi-debug--where)))

(defun urushi-debug--message (message)
  "Write down MESSAGE, and let it be shown as it would be."
  (urushi--log "message %s" message)
  nil)

(defun urushi-debug--error (error context caller)
  "Write down ERROR, which CALLER signalled in CONTEXT.
The error is then reported as it would have been."
  (urushi--log "error %S%s from %S: %s" error (or context "") caller
              (urushi-debug--where))
  (command-error-default-function error context caller))

(defun urushi-debug--tell-host (on)
  "Tell the host to write down what it sees, if ON, or to stop."
  (when (urushi-available-p)
    (urushi--send (list :type "debug" :on (if on t :false)))))

;;;###autoload
(define-minor-mode urushi-debug-mode
  "Write down what happens, in the log the host keeps beside it."
  :global t
  (if urushi-debug-mode
      (progn
        (add-hook 'post-command-hook #'urushi-debug--command 90)
        (add-function :after after-focus-change-function #'urushi-debug--focus)
        (add-hook 'set-message-functions #'urushi-debug--message)
        (setq command-error-function #'urushi-debug--error)
        (urushi-debug--tell-host t)
        (urushi--log "debug on: %s" (urushi-debug--where)))
    (remove-hook 'post-command-hook #'urushi-debug--command)
    (remove-function after-focus-change-function #'urushi-debug--focus)
    (remove-hook 'set-message-functions #'urushi-debug--message)
    (when (eq command-error-function #'urushi-debug--error)
      (setq command-error-function #'command-error-default-function))
    (urushi-debug--tell-host nil)
    (urushi--log "debug off")))

(provide 'urushi-debug)

;;; urushi-debug.el ends here
