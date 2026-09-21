;;; urusi-layout-test.el --- Tests for urusi-layout.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l test/urusi-layout-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urusi-layout)

(defmacro urusi-layout-test--with (layout &rest body)
  "Run BODY with LAYOUT as the layout, and nothing changed about it."
  (declare (indent 1))
  `(let ((urusi-layout ,layout)
         (urusi-layout--state (make-hash-table :test #'eq)))
     (cl-letf (((symbol-function 'urusi-screen--after-command) #'ignore)
               ((symbol-function 'urusi-screen-frame-site)
                (lambda (_frame) '(Grid :Name "urusi-frame"))))
       ,@body)))

(defun urusi-layout-test--xaml ()
  "Return the layout as XAML."
  (car (urusi--compile (urusi-layout-component nil))))

(defconst urusi-layout-test--three
  '(row (panel :id left :size 200 :content (TextBlock :Text "left"))
        (panel :id middle :content emacs)
        (panel :id right :weight 2 :content nil))
  "Three panels side by side.")

(ert-deftest urusi-layout-row-with-splitters ()
  "Parts side by side, sized as written, with a splitter between each two."
  (urusi-layout-test--with urusi-layout-test--three
    (let ((xaml (urusi-layout-test--xaml)))
      (should (string-match-p (concat "<ColumnDefinition Width=\"200\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"1\\*\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"2\\*\" />")
                              xaml))
      (should (string-match-p "x:Name=\"urusi-splitter:h:left:middle\" Grid.Column=\"1\"" xaml))
      (should (string-match-p "x:Name=\"urusi-splitter:h:middle:right\" Grid.Column=\"3\"" xaml))
      (should (string-match-p "<Grid x:Name=\"urusi-frame\" />" xaml)))))

(ert-deftest urusi-layout-hidden-part-leaves-its-room ()
  "A hidden part is not there, and neither is the splitter beside it."
  (urusi-layout-test--with urusi-layout-test--three
    (urusi-layout-hide 'left)
    (let ((xaml (urusi-layout-test--xaml)))
      (should-not (string-match-p "left" xaml))
      (should (string-match-p (concat "<ColumnDefinition Width=\"1\\*\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"2\\*\" />")
                              xaml)))
    (urusi-layout-toggle 'left)
    (should (string-match-p "urusi-splitter:h:left:middle" (urusi-layout-test--xaml)))))

(ert-deftest urusi-layout-group-with-nothing-shown-is-hidden ()
  "A column whose parts are all hidden takes no room either."
  (urusi-layout-test--with '(row (panel :id a :content emacs)
                                 (column :id side
                                         (panel :id b :content nil)
                                         (panel :id c :content nil)))
    (urusi-layout-hide 'b)
    (urusi-layout-hide 'c)
    (should-not (string-match-p "splitter" (urusi-layout-test--xaml)))))

(ert-deftest urusi-layout-resize-and-reset ()
  "A size set from Lisp replaces the one written, until the layout is reset."
  (urusi-layout-test--with urusi-layout-test--three
    (urusi-layout-resize 'left 320)
    (should (string-match-p "<ColumnDefinition Width=\"320\" />" (urusi-layout-test--xaml)))
    (urusi-layout-reset)
    (should (string-match-p "<ColumnDefinition Width=\"200\" />" (urusi-layout-test--xaml)))))

(ert-deftest urusi-layout-splitter-let-go ()
  "Where a splitter is let go is the size of the part that has one."
  (urusi-layout-test--with urusi-layout-test--three
    ;; The part before has a size of its own, and keeps it.
    (urusi-layout--dragged 'splitter '(:name "urusi-splitter:h:left:middle"
                                       :before 260 :after 700))
    (should (equal (urusi-layout--get (urusi-layout--find 'left) :size) 260))
    ;; Neither has one: the one before is given one.
    (urusi-layout--dragged 'splitter '(:name "urusi-splitter:h:middle:right"
                                       :before 500 :after 460))
    (should (equal (urusi-layout--get (urusi-layout--find 'middle) :size) 500))
    (should-not (urusi-layout--get (urusi-layout--find 'right) :size))))

(ert-deftest urusi-layout-panel-properties ()
  "A panel's own background and padding are on the element around it."
  (urusi-layout-test--with '(panel :id only :background "#101010" :padding 8
                                   :content (TextBlock :Text "x"))
    (should (string-match-p "Background=\"#101010\" Padding=\"8\">"
                            (urusi-layout-test--xaml)))))

;;; urusi-layout-test.el ends here
