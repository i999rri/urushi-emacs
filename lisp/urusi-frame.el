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
  (let* ((root (urusi-root-frame))
         (title (format-mode-line frame-title-format nil
                                  (frame-selected-window root)))
         (state (urusi-frame--state-of root)))
    (unless (equal title urusi-frame--title)
      (setq urusi-frame--title title)
      (urusi-call "window.title" (list :title title)))
    (unless (equal state urusi-frame--state)
      ;; Nothing is said the first time round: the window starts as it
      ;; is, and Emacs's frame starts as a frame does, which is normal.
      (when urusi-frame--state
        (urusi-call "window.state" (list :state state)))
      (setq urusi-frame--state state))))

(defcustom urusi-frame-close-function #'urusi-frame-ask-to-leave
  "Function called when the host window is asked to close.
It takes no arguments, and is to leave or not: the window does not
close by itself, since leaving is Emacs's to decide, the way it always
has been.

`urusi-frame-ask-to-leave' asks in a dialog of the host's, which is
what closing a window of the system asks in.  `save-buffers-kill-emacs'
asks a buffer at a time in the echo area, which is what Emacs asks in."
  :type 'function)

(defun urusi-frame-unsaved-buffers ()
  "Return the buffers of files with changes that are not saved."
  (seq-filter (lambda (buffer)
                (and (buffer-file-name buffer)
                     (buffer-modified-p buffer)))
              (buffer-list)))

(defun urusi-frame--unsaved-message (buffers)
  "Return what to ask about BUFFERS, the ones that are not saved.
They are named rather than counted: what is about to be lost is worth
reading before answering, and a dialog with room for eight of them has
room for the names."
  (let* ((shown (seq-take buffers 8))
         (rest (- (length buffers) (length shown))))
    (concat (if (cdr buffers)
                (format "%d files have changes that are not saved:"
                        (length buffers))
              "One file has changes that are not saved:")
            "\n\n"
            (mapconcat (lambda (buffer) (concat "    " (buffer-name buffer)))
                       shown "\n")
            (if (> rest 0) (format "\n    and %d more" rest) ""))))

(defun urusi-frame-ask-to-leave ()
  "Ask about the buffers that are not saved, in a dialog of the host's.
Nothing is asked where there are none: closing a window that has
nothing to lose closes it.

Emacs goes on while the dialog is up, so whatever is to happen after it
is answered happens in the answer and not here."
  (if-let* ((unsaved (urusi-frame-unsaved-buffers)))
      (urusi-ask (urusi-frame--unsaved-message unsaved)
                 :title "Leave Emacs?"
                 :accept "Save and leave"
                 :other "Leave without saving"
                 :cancel "Stay"
                 :then (lambda (answer)
                         (pcase answer
                           ('accept (save-some-buffers t) (kill-emacs))
                           ('other (kill-emacs))
                           (_ nil))))
    (kill-emacs)))

(defun urusi-frame--close (event _message)
  "Leave Emacs when the host window is asked to close, as EVENT says.
The window does not close by itself: leaving is Emacs's to decide, the
way it always has been, so the buffers that are not saved are asked
about first and the answer may be not to leave at all."
  (when (eq event 'close)
    ;; Not from here: this runs while the host's messages are being
    ;; read, and what it asks waits for more of them.
    (run-at-time 0 nil urusi-frame-close-function)))

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
