;;; urusi-statusbar-test.el --- Tests for urusi-statusbar.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l lisp/test/urusi-statusbar-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urusi-statusbar)

(defun urusi-statusbar-test--bar (&rest arguments)
  "Return the XAML of the bar `urusi-statusbar' makes of ARGUMENTS."
  (let ((rows (nth 3 (urusi--compile (apply #'urusi-statusbar nil arguments)))))
    (cadr (cadr (assoc "urusi-statusbar" rows)))))

(ert-deftest urusi-statusbar-segments-take-properties ()
  "A segment written with properties draws with them, and one without as it is."
  (let ((xaml (urusi-statusbar-test--bar
               :right '((urusi-statusbar-position :FontSize 12)
                        urusi-statusbar-buffer))))
    (should (string-match-p "<TextBlock Text=\"Ln 1, Col 1\" FontSize=\"12\"" xaml))
    (should (string-match-p "<TextBlock Text=\"\\*scratch\\*\" VerticalAlignment" xaml))))

(ert-deftest urusi-statusbar-colours-its-text ()
  "The colour of the bar's text goes to its text and buttons, not to the Grid."
  (let ((xaml (urusi-statusbar-test--bar :Height 24 :Foreground "#000000")))
    (should (string-match-p "<Grid [^>]*Height=\"24\">" xaml))
    (should-not (string-match-p "<Grid [^>]*Foreground" xaml))
    (should (string-match-p "<Setter Property=\"Foreground\" Value=\"#000000\" />" xaml))
    (should (string-match-p "x:Key=\"ButtonForeground\" Color=\"#000000\"" xaml))))

(ert-deftest urusi-statusbar-names-a-cc-mode-without-its-flags ()
  "A mode of CC Mode is named without how it is set."
  (require 'cc-mode)
  (with-current-buffer (get-buffer-create "urusi-statusbar-java")
    (java-mode)
    (c-update-modeline)
    (set-window-buffer nil (current-buffer))
    (cl-letf (((symbol-function 'format-mode-line)
               (lambda (format &rest _) (format "%s" format))))
      (should (equal (plist-get (cdr (urusi-statusbar-major-mode (selected-window))) :Text)
                     "Java")))
    (kill-buffer)))

;;; urusi-statusbar-test.el ends here
