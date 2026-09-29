;;; urushi-layout-test.el --- Tests for urushi-layout.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l lisp/test/urushi-layout-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urushi-layout)

(defmacro urushi-layout-test--with (layout &rest body)
  "Run BODY with LAYOUT as the layout, and nothing changed about it."
  (declare (indent 1))
  `(let ((urushi-layout ,layout)
         (urushi-layout--state (make-hash-table :test #'eq)))
     (cl-letf (((symbol-function 'urushi-screen--after-command) #'ignore)
               ((symbol-function 'urushi-screen-frame-site)
                (lambda (_frame) '(Grid :Name "urushi-frame"))))
       ,@body)))

(defun urushi-layout-test--xaml ()
  "Return the layout as XAML."
  (car (urushi--compile (urushi-layout-component nil))))

(defconst urushi-layout-test--three
  '(row (panel :id left :size 200 :content (TextBlock :Text "left"))
        (panel :id middle :content emacs)
        (panel :id right :weight 2 :content nil))
  "Three panels side by side.")

(ert-deftest urushi-layout-row-with-splitters ()
  "Parts side by side, sized as written, with a splitter between each two."
  (urushi-layout-test--with urushi-layout-test--three
    (let ((xaml (urushi-layout-test--xaml)))
      (should (string-match-p (concat "<ColumnDefinition Width=\"200\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"1\\*\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"2\\*\" />")
                              xaml))
      (should (string-match-p "x:Name=\"urushi-splitter:h:left:middle\" Grid.Column=\"1\"" xaml))
      (should (string-match-p "x:Name=\"urushi-splitter:h:middle:right\" Grid.Column=\"3\"" xaml))
      (should (string-match-p "<Grid x:Name=\"urushi-frame\" />" xaml)))))

(ert-deftest urushi-layout-hidden-part-leaves-its-room ()
  "A hidden part is not there, and neither is the splitter beside it."
  (urushi-layout-test--with urushi-layout-test--three
    (urushi-layout-hide 'left)
    (let ((xaml (urushi-layout-test--xaml)))
      (should-not (string-match-p "left" xaml))
      (should (string-match-p (concat "<ColumnDefinition Width=\"1\\*\" />"
                                      "<ColumnDefinition Width=\"4\" />"
                                      "<ColumnDefinition Width=\"2\\*\" />")
                              xaml)))
    (urushi-layout-toggle 'left)
    (should (string-match-p "urushi-splitter:h:left:middle" (urushi-layout-test--xaml)))))

(ert-deftest urushi-layout-group-with-nothing-shown-is-hidden ()
  "A column whose parts are all hidden takes no room either."
  (urushi-layout-test--with '(row (panel :id a :content emacs)
                                 (column :id side
                                         (panel :id b :content nil)
                                         (panel :id c :content nil)))
    (urushi-layout-hide 'b)
    (urushi-layout-hide 'c)
    (should-not (string-match-p "splitter" (urushi-layout-test--xaml)))))

(ert-deftest urushi-layout-resize-and-reset ()
  "A size set from Lisp replaces the one written, until the layout is reset."
  (urushi-layout-test--with urushi-layout-test--three
    (urushi-layout-resize 'left 320)
    (should (string-match-p "<ColumnDefinition Width=\"320\" />" (urushi-layout-test--xaml)))
    (urushi-layout-reset)
    (should (string-match-p "<ColumnDefinition Width=\"200\" />" (urushi-layout-test--xaml)))))

(ert-deftest urushi-layout-splitter-let-go ()
  "Where a splitter is let go is the size of the part that has one."
  (urushi-layout-test--with urushi-layout-test--three
    ;; The part before has a size of its own, and keeps it.
    (urushi-layout--dragged 'splitter '(:name "urushi-splitter:h:left:middle"
                                       :before 260 :after 700))
    (should (equal (urushi-layout--get (urushi-layout--find 'left) :size) 260))
    ;; Neither has one: the one before is given one.
    (urushi-layout--dragged 'splitter '(:name "urushi-splitter:h:middle:right"
                                       :before 500 :after 460))
    (should (equal (urushi-layout--get (urushi-layout--find 'middle) :size) 500))
    (should-not (urushi-layout--get (urushi-layout--find 'right) :size))))

(ert-deftest urushi-layout-panel-properties ()
  "A panel's own background and padding are on the element around it."
  (urushi-layout-test--with '(panel :id only :background "#101010" :padding 8
                                   :content (TextBlock :Text "x"))
    (should (string-match-p "Background=\"#101010\" Padding=\"8\">"
                            (urushi-layout-test--xaml)))))

(ert-deftest urushi-layout-panel-border-from-a-function ()
  "A panel's border is its own, and a function gives its value as it is drawn."
  (urushi-layout-test--with `(panel :id only
                                   :border-brush ,(lambda (_frame) "#FF8000")
                                   :border-thickness "0,1,0,0"
                                   :content (TextBlock :Text "x"))
    (should (string-match-p "BorderBrush=\"#FF8000\" BorderThickness=\"0,1,0,0\">"
                            (urushi-layout-test--xaml)))))

(defconst urushi-layout-test--floating
  '(layer (panel :id editor :content emacs)
          (column (panel :content nil)
                  (panel :id output :size 200 :content (TextBlock :Text "out"))))
  "An output that floats over Emacs, at the bottom.")

(ert-deftest urushi-layout-layer-puts-parts-over-each-other ()
  "A layer's parts share its one cell, the later over the earlier."
  (urushi-layout-test--with urushi-layout-test--floating
    (let ((xaml (urushi-layout-test--xaml)))
      (should (string-match-p "\\`<Grid [^>]*><Border><Grid x:Name=\"urushi-frame\" />" xaml))
      (should-not (string-match-p "urushi-frame\" Grid\\.Row" xaml))
      (should (string-match-p (concat "<RowDefinition Height=\"1\\*\" />"
                                      "<RowDefinition Height=\"4\" />"
                                      "<RowDefinition Height=\"200\" />")
                              xaml))
      (should (string-match-p "urushi-splitter:v:-:output" xaml)))))

(ert-deftest urushi-layout-layer-z-index-and-hiding ()
  "A part of a layer can say how high it is, and hidden it is not there."
  (urushi-layout-test--with '(layer (panel :id top :z-index 1 :content (TextBlock :Text "top"))
                                   (panel :id under :content (TextBlock :Text "under")))
    (should (string-match-p "<Border Canvas.ZIndex=\"1\"" (urushi-layout-test--xaml)))
    (urushi-layout-hide 'top)
    (should-not (string-match-p "top" (urushi-layout-test--xaml)))
    (urushi-layout-hide 'under)
    (should (equal (urushi-layout-test--xaml) (car (urushi--compile '(Border)))))))

;;; urushi-layout-test.el ends here
