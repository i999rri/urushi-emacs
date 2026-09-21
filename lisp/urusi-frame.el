;;; urusi-frame.el --- The host window as the Emacs frame  -*- lexical-binding: t; -*-

;;; Commentary:

;; Makes what Emacs does to its frame happen to the host window, so that
;; what already worked in Emacs goes on working: `frame-title-format'
;; names the window, and `toggle-frame-fullscreen' and
;; `toggle-frame-maximized' fill the screen with it.
;;
;; The frame is on no screen of its own.  The host draws it, in a window
;; that is the host's, so what Emacs decides about its frame is read
;; here after each command and passed on to that window.
;;
;; It is passed on when Emacs changes its mind and not otherwise.  The
;; window can be maximized from its own title bar without Emacs knowing,
;; and saying again what Emacs last thought would put it straight back.

;;; Code:

(require 'urusi)

(defgroup urusi-frame nil
  "The host window as the Emacs frame."
  :group 'urusi)

(defvar urusi-frame--title nil
  "The title the host window was last given, or nil.")

(defvar urusi-frame--state nil
  "What Emacs last wanted the frame to be, as the host names it.")

(defun urusi-frame--state-of (frame)
  "Return what Emacs wants FRAME to be, as a state the host knows."
  (pcase (frame-parameter frame 'fullscreen)
    ((or 'fullboth 'fullscreen) "fullscreen")
    ('maximized "maximized")
    (_ "normal")))

(defun urusi-frame--sync ()
  "Pass on to the host window whatever Emacs has changed about its frame."
  (let ((title (format-mode-line frame-title-format))
        (state (urusi-frame--state-of (selected-frame))))
    (unless (equal title urusi-frame--title)
      (setq urusi-frame--title title)
      (urusi-call "window.title" (list :title title)))
    (unless (equal state urusi-frame--state)
      ;; Nothing is said the first time round: the window starts as it
      ;; is, and Emacs's frame starts as a frame does, which is normal.
      (when urusi-frame--state
        (urusi-call "window.state" (list :state state)))
      (setq urusi-frame--state state))))

(defun urusi-frame--close (event _message)
  "Leave Emacs when the host window is asked to close, as EVENT says.
The window does not close by itself: leaving is Emacs's to decide, the
way it always has been, so the buffers that are not saved are asked
about first and the answer may be not to leave at all."
  (when (eq event 'close)
    ;; Not from here: this runs while the host's messages are being
    ;; read, and what it asks waits for more of them.
    (run-at-time 0 nil #'save-buffers-kill-emacs)))

(defvar urusi-frame--pending nil
  "Timer that will pass the frame on, when one is waiting to run.")

(defun urusi-frame--after-command ()
  "Pass the frame on once what is happening now has finished happening."
  (unless urusi-frame--pending
    (setq urusi-frame--pending
          (urusi-when-idle
           (lambda ()
             (setq urusi-frame--pending nil)
             (condition-case err
                 (urusi-frame--sync)
               (error (urusi--log "frame: %S" err))))))))

;;;###autoload
(define-minor-mode urusi-frame-mode
  "Make what Emacs does to its frame happen to the host window."
  :global t
  (if urusi-frame-mode
      (progn
        (setq urusi-frame--title nil
              urusi-frame--state nil)
        (add-hook 'post-command-hook #'urusi-frame--after-command)
        (add-hook 'urusi-after-event-hook #'urusi-frame--after-command)
        (add-hook 'urusi-host-event-functions #'urusi-frame--close)
        (urusi-frame--after-command))
    (remove-hook 'post-command-hook #'urusi-frame--after-command)
    (remove-hook 'urusi-after-event-hook #'urusi-frame--after-command)
    (remove-hook 'urusi-host-event-functions #'urusi-frame--close)))

(provide 'urusi-frame)
;;; urusi-frame.el ends here
