;;; Where Emacs says the point is on the screen.  -*- lexical-binding: t; -*-

;; The cursor stops one character short of the end of the text, so ask
;; Emacs where it thinks the point is at every position of a line and
;; see where the answer stops changing.
;;
;;   emacs.exe -Q -l scripts/verify/probe-cursor.el

(defun probe-cursor--say (out format &rest arguments)
  (princ (apply #'format format arguments) out)
  (princ "\n" out))

(defun probe-cursor ()
  (with-temp-file "probe-cursor.txt"
    (let ((out (current-buffer)))
      (with-current-buffer (get-buffer-create "*probe*")
        (switch-to-buffer (current-buffer))
        (erase-buffer)
        (insert "abcあいう")
        (redisplay)
        (probe-cursor--say out "buffer %S, point-max %d, cell %d"
                           (buffer-string) (point-max) (default-font-width))
        (dolist (position (number-sequence (point-min) (point-max)))
          (goto-char position)
          (redisplay)
          (let ((posn (posn-at-point position)))
            (probe-cursor--say
             out "point %d char %S -> posn-x-y %S, window %S"
             position
             (if (< position (point-max)) (char-after position) 'eob)
             (and posn (posn-x-y posn))
             (and posn (posn-object-x-y posn))))))))
  (kill-emacs))

(run-with-idle-timer 1 nil #'probe-cursor)
