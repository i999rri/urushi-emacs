;;; urusi-titlebar-test.el --- Tests for urusi-titlebar.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l test/urusi-titlebar-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urusi-titlebar)

(defun urusi-titlebar-test--attributes (xaml name)
  "Return how many times attribute NAME is set in XAML's first element."
  (let ((first (substring xaml 0 (string-search ">" xaml))))
    (cl-count-if (lambda (part) (string-prefix-p (concat name "=") part))
                 (split-string first " "))))

(ert-deftest urusi-titlebar-button-overrides-once ()
  "A property given for a button replaces its own rather than repeating it."
  (let ((xaml (car (urusi--compile
                    (urusi-titlebar-button "b" ?x #'ignore :Width 60 :Height 40)))))
    (should (= 1 (urusi-titlebar-test--attributes xaml "Width")))
    (should (string-match-p "Width=\"60\"" xaml))
    (should (string-match-p "Height=\"40\"" xaml))))

(ert-deftest urusi-titlebar-parts-make-a-title-bar ()
  "A title bar made of the parts compiles, with the title as a row of its own."
  ;; A batch Emacs has no frame to make a title for.
  (cl-letf (((symbol-function 'format-mode-line) (lambda (&rest _) "the title")))
    (urusi-titlebar-test--title-bar)))

(defun urusi-titlebar-test--title-bar ()
  "Check a title bar made of the parts."
  (let* ((result (urusi--compile
                  `(Grid :Name "urusi-titlebar" :Height 40
                         ,(urusi-titlebar-title :Margin "12,0,0,0" :Grid.Column 0)
                         ,(urusi-titlebar-buttons :height 40 :Grid.Column 1)))))
    (should (string-match-p "<StackPanel Orientation=\"Horizontal\" Grid.Column=\"1\">"
                            (nth 0 result)))
    (should (string-match-p "<Grid x:Name=\"urusi-titlebar-title\" Grid.Column=\"0\""
                            (nth 0 result)))
    (should (equal (mapcar (lambda (event) (plist-get event :name)) (nth 1 result))
                   '("urusi-minimize" "urusi-maximize" "urusi-close")))
    (let ((title (cdar (cdr (assoc "urusi-titlebar-title" (nth 3 result))))))
      (should (string-match-p "Text=\"the title\"" title))
      (should (string-match-p "Margin=\"12,0,0,0\"" title))
      (should-not (string-match-p "Grid.Column" title)))))

;;; urusi-titlebar-test.el ends here
