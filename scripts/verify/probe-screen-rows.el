;;; What `window-screen-rows' says about a screen.  -*- lexical-binding: t; -*-

;; The screen is read out of Emacs rather than worked out again, so the
;; first thing to know is what it reads.  Shows a buffer with a face, a
;; wide character and a mode line, and prints every line of it.
;;
;;   emacs.exe -Q -l scripts/verify/probe-screen-rows.el

(defun probe-screen-rows ()
  ;; An error in a timer leaves Emacs sitting there with nothing to
  ;; show for it, and this has no one watching.
  (condition-case err
      (probe-screen-rows--run)
    (error (with-temp-file "probe-screen-rows.txt"
             (insert (format "%S\n\n" err))
             (insert (format "cursor %S\n\n" (ignore-errors (window-screen-cursor))))
             (insert (format "lines %S\n" (ignore-errors (window-screen-rows)))))))
  (kill-emacs))

(defun probe-screen-rows--run ()
  (with-temp-file "probe-screen-rows.txt"
    (let ((out (current-buffer)))
      (with-current-buffer (get-buffer-create "*probe*")
        (switch-to-buffer (current-buffer))
        (erase-buffer)
        (insert "abc" (propertize "RED" 'face 'error) " あいう\n")
        (insert "second line\n")
        (goto-char 5)
        (redisplay)
        (princ (format "cursor %S\n\n" (window-screen-cursor)) out)
        (dolist (line (window-screen-rows))
          (princ (format "line y=%s h=%s kind=%s start=%s\n"
                         (plist-get line :y) (plist-get line :height)
                         (plist-get line :kind) (plist-get line :start))
                 out)
          (dolist (run (plist-get line :runs))
            (princ (format "  x=%-4s w=%-4s %S fg=%S bg=%S family=%S size=%S\n"
                           (plist-get run :x) (plist-get run :width)
                           (plist-get run :text)
                           (plist-get run :foreground)
                           (plist-get run :background)
                           (plist-get run :family)
                           (plist-get run :size))
                   out)))))))

(run-with-idle-timer 1 nil #'probe-screen-rows)
