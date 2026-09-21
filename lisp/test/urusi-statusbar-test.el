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

(ert-deftest urusi-statusbar-fill-takes-the-rest ()
  "Segments that fill go between the ends, the last of them in what is left."
  (let ((xaml (urusi-statusbar-test--bar
               :fill (list (lambda (_) (urusi-statusbar-text "a"))
                           (lambda (_) (urusi-statusbar-text "b"))))))
    (should (string-match-p (concat "<Grid Grid.Column=\"1\"><Grid.ColumnDefinitions>"
                                    "<ColumnDefinition Width=\"Auto\" />"
                                    "<ColumnDefinition Width=\"\\*\" />")
                            xaml))
    (should (string-match-p "<TextBlock Text=\"b\" [^>]*Grid.Column=\"1\"" xaml))))

(ert-deftest urusi-statusbar-says-the-message ()
  "The message segment says the first line of what the echo area says."
  (cl-letf (((symbol-function 'current-message) (lambda () "one\ntwo")))
    (should (equal (plist-get (cdr (urusi-statusbar-message nil)) :Text) "one")))
  (cl-letf (((symbol-function 'current-message) (lambda () nil)))
    (should-not (urusi-statusbar-message nil))))

(ert-deftest urusi-statusbar-takes-messages-said-while-typing ()
  "A message said while the minibuffer is typed in goes to the bar, and only then."
  (unwind-protect
      (cl-letf (((symbol-function 'urusi-screen--after-command) #'ignore)
                ((symbol-function 'current-message) (lambda () nil)))
        (cl-letf (((symbol-function 'active-minibuffer-window) (lambda () nil)))
          (should-not (urusi-statusbar-take-minibuffer-message "not typing"))
          (should-not (urusi-statusbar-message nil)))
        (cl-letf (((symbol-function 'active-minibuffer-window) (lambda () 'window)))
          (should (urusi-statusbar-take-minibuffer-message "while typing"))
          (should (equal (plist-get (cdr (urusi-statusbar-message nil)) :Text)
                         "while typing")))
        ;; Gone once the minibuffer is left.
        (run-hooks 'minibuffer-exit-hook)
        (should-not (urusi-statusbar-message nil))
        (should-not (memq #'urusi-statusbar--forget-minibuffer-message
                          minibuffer-exit-hook)))
    (urusi-statusbar--forget-minibuffer-message)))

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
