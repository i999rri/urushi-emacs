;;; urushi-frame-test.el --- Tests for urushi-frame.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l lisp/test/urushi-frame-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urushi-frame)

(defun urushi-frame-test--unsaved (name)
  "Return a buffer called NAME that stands for a file with changes in it."
  (let ((buffer (generate-new-buffer name)))
    (with-current-buffer buffer
      (setq buffer-file-name (expand-file-name name temporary-file-directory))
      (insert "something")
      (set-buffer-modified-p t))
    buffer))

(ert-deftest urushi-frame-find-what-is-not-saved ()
  "A buffer of a file with changes is one; one with none, and one that
is no file, are not."
  (let ((changed (urushi-frame-test--unsaved "changed.el"))
        (saved (urushi-frame-test--unsaved "saved.el"))
        (scratch (generate-new-buffer "not a file")))
    (unwind-protect
        (progn
          (with-current-buffer saved (set-buffer-modified-p nil))
          (with-current-buffer scratch (insert "typed"))
          (let ((unsaved (urushi-frame-unsaved-buffers)))
            (should (memq changed unsaved))
            (should-not (memq saved unsaved))
            (should-not (memq scratch unsaved))))
      (dolist (buffer (list changed saved scratch))
        (with-current-buffer buffer (set-buffer-modified-p nil))
        (kill-buffer buffer)))))

(ert-deftest urushi-frame-name-what-is-not-saved ()
  "What is asked names the files, and says how many were left out."
  (let ((one (list (get-buffer-create "one.el"))))
    (should (string-match-p "\\`One file has changes"
                            (urushi-frame--unsaved-message one)))
    (should (string-match-p "    one\\.el"
                            (urushi-frame--unsaved-message one))))

  (let ((two (list (get-buffer-create "one.el") (get-buffer-create "two.el"))))
    (should (string-match-p "\\`2 files have changes"
                            (urushi-frame--unsaved-message two))))

  ;; Eight are named and the rest counted: a dialog is not a list of
  ;; every buffer there is.
  (let ((many (mapcar (lambda (at) (get-buffer-create (format "%d.el" at)))
                      (number-sequence 1 11))))
    (let ((said (urushi-frame--unsaved-message many)))
      (should (string-match-p "\\`11 files have changes" said))
      (should (string-match-p "    8\\.el" said))
      (should-not (string-match-p "    9\\.el" said))
      (should (string-match-p "and 3 more" said)))))

;;; urushi-frame-test.el ends here
