;;; urusi-tabs-test.el --- Tests for urusi-tabs.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l lisp/test/urusi-tabs-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urusi-tabs)
(require 'tab-line)

(ert-deftest urusi-tabs-read-the-tab-bar ()
  "The tabs of the tab bar come as tabs, and selecting one selects it."
  (let ((tab-bar-mode-was tab-bar-mode))
    (unwind-protect
        (progn
          (tab-bar-mode 1)
          (tab-bar-new-tab)
          (tab-bar-rename-tab "second")
          (let ((tabs (urusi-tabs-tab-bar-tabs)))
            (should (= 2 (length tabs)))
            (should (equal (plist-get (nth 1 tabs) :name) "second"))
            (should (plist-get (nth 1 tabs) :current))
            (should (eq (plist-get (nth 0 tabs) :face) 'tab-bar-tab-inactive))
            (funcall (plist-get (nth 0 tabs) :select))
            (should (plist-get (nth 0 (urusi-tabs-tab-bar-tabs)) :current))))
      (while (< 1 (length (funcall tab-bar-tabs-function)))
        (tab-bar-close-tab))
      (tab-bar-mode (if tab-bar-mode-was 1 -1)))))

(ert-deftest urusi-tabs-read-the-tab-line ()
  "The tabs of a window come as its buffers, the one it shows the current."
  (let ((a (get-buffer-create "urusi-tabs-a"))
        (b (get-buffer-create "urusi-tabs-b")))
    (unwind-protect
        (let ((tab-line-tabs-function (lambda () (list a b))))
          (set-window-buffer nil b)
          (let ((tabs (urusi-tabs-tab-line-tabs)))
            (should (equal (mapcar (lambda (tab) (plist-get tab :name)) tabs)
                           '("urusi-tabs-a" "urusi-tabs-b")))
            (should-not (plist-get (nth 0 tabs) :current))
            (should (plist-get (nth 1 tabs) :current))
            (funcall (plist-get (nth 0 tabs) :select))
            (should (eq (window-buffer) a))))
      (kill-buffer a)
      (kill-buffer b))))

(ert-deftest urusi-tabs-strip-carries-their-actions ()
  "A strip of tabs has a click for each tab, and one for each close button."
  (let* ((tabs (list (list :name "one" :current t :select #'ignore :close #'ignore)
                     (list :name "two" :select #'ignore)))
         (result (urusi--compile
                  `(Grid (Rows :key "tabs" ,(urusi-tabs tabs :key "strip" :Height 32)))))
         (row (cadr (assoc "tabs" (nth 3 result)))))
    (should (string-match-p "<ScrollViewer .* Height=\"32\"" (cadr row)))
    (should (string-match-p "Text=\"one\"" (cadr row)))
    ;; Three: the two tabs, and the close button of the first.
    (should (= 3 (length (cddr row))))
    (should (cl-every (lambda (event) (string-prefix-p "tabs/strip/" (plist-get event :id)))
                      (cddr row)))))

(ert-deftest urusi-tabs-strip-can-stand-down-the-side ()
  "Tabs down the side scroll up and down, and tabs that are cut off do not scroll."
  (let ((down (car (urusi--compile (urusi-tabs nil :orientation "Vertical"))))
        (cut (car (urusi--compile (urusi-tabs nil :scroll nil)))))
    (should (string-match-p "VerticalScrollMode=\"Enabled\"" down))
    (should (string-match-p "Orientation=\"Vertical\"" down))
    (should (string-prefix-p "<Grid" cut))))

;;; urusi-tabs-test.el ends here
