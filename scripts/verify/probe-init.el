;;; How long the user's init file takes, and what it said.  -*- lexical-binding: t; -*-

;; The screen does not appear until the init file has been read, so an
;; init file that takes a long time looks exactly like one that hangs.
;; This is the same Emacs reading the same init file outside the host,
;; which says which of the two it is.
;;
;;   emacs.exe -l scripts/verify/probe-init.el
;;
;; Note the missing -Q: the point is to read the init file.

(defun probe-init ()
  (with-temp-file "probe-init.txt"
    (insert (format "init file: %s\n" user-init-file))
    (insert (format "init time: %s\n" (emacs-init-time)))
    (insert (format "features:  %d\n" (length features)))
    (insert (format "frame:     %S\n" (frame-parameter nil 'window-system)))
    (insert "\nwarnings:\n")
    (if-let* ((warnings (get-buffer "*Warnings*")))
        (insert (with-current-buffer warnings (buffer-string)))
      (insert "  none\n"))
    (insert "\nmessages (last 40 lines):\n")
    (with-current-buffer "*Messages*"
      (insert-buffer-substring
       (current-buffer)
       (save-excursion (goto-char (point-max)) (forward-line -40) (point))
       (point-max))))
  (kill-emacs))

;; Called outright rather than from a hook: a file given with -l is
;; loaded after the init file and after `after-init-hook' has run, so
;; adding to that hook here would be adding to a hook that is over.
(probe-init)
