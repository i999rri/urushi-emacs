;;; urusi-test.el --- Tests for urusi.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l test/urusi-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urusi)

(defconst urusi-test--ns
  (concat " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\""
          " xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\""))

(ert-deftest urusi-compile-plain-tree ()
  (let ((result (urusi--compile '(StackPanel :Padding 24
                                             (TextBlock :Text "hi" :FontSize 28)
                                             (Button :IsEnabled nil)))))
    (should (equal (nth 0 result)
                   (concat "<StackPanel" urusi-test--ns " Padding=\"24\">"
                           "<TextBlock Text=\"hi\" FontSize=\"28\" />"
                           "<Button IsEnabled=\"False\" />"
                           "</StackPanel>")))
    (should (null (nth 1 result)))))

(ert-deftest urusi-compile-escapes-attributes-and-text ()
  (let ((xaml (car (urusi--compile '(TextBlock :Text "a<b & \"c\"" "x > y")))))
    (should (equal xaml (concat "<TextBlock" urusi-test--ns
                                " Text=\"a&lt;b &amp; &quot;c&quot;\">x &gt; y</TextBlock>")))))

(ert-deftest urusi-compile-names-elements-with-handlers ()
  (let* ((click (lambda () 'clicked))
         (changed (lambda (args) args))
         (result (urusi--compile
                  `(StackPanel
                    (Button :Content "A" :on-Click ,click)
                    (TextBox :Name "query" :on-TextChanged ,changed)))))
    (should (string-match-p "<Button x:Name=\"urusi1\" Content=\"A\" />" (nth 0 result)))
    (should (string-match-p "<TextBox x:Name=\"query\" />" (nth 0 result)))
    (should (equal (nth 1 result)
                   '((:name "urusi1" :event "Click" :id 1)
                     (:name "query" :event "TextChanged" :id 2))))
    (should (eq (gethash 1 (nth 2 result)) click))
    (should (eq (gethash 2 (nth 2 result)) changed))))

(ert-deftest urusi-compile-rejects-non-function-handler ()
  (should-error (urusi--compile '(Button :on-Click "not a function"))))

(ert-deftest urusi-round-trip-with-fake-host ()
  "Render through a fake host, then deliver an event back to the handler."
  (let ((posted nil)
        (from-host nil)
        (clicked nil))
    ;; Stand in for the host: these three are what libemacs.dll adds when
    ;; a host application loads it, and are missing in a plain Emacs.
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message) (push message posted) t))
              ((symbol-function 'w32-host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (unwind-protect
          (progn
            (urusi-start)
            (urusi-render `(Button :Content "OK" :on-Click ,(lambda () (setq clicked t))))
            (setq posted (nreverse posted))
            (should (equal (json-parse-string (nth 0 posted) :object-type 'plist)
                           '(:type "hello" :version 1)))
            (let ((screen (json-parse-string (nth 1 posted) :object-type 'plist)))
              (should (equal (plist-get screen :type) "screen"))
              (should (equal (plist-get screen :events)
                             [(:name "urusi1" :event "Click" :id 1)])))
            ;; The host reports the click; the id arrives as a JSON number.
            (push "{\"type\":\"event\",\"id\":1,\"args\":{}}" from-host)
            (let* ((after nil)
                   (urusi-after-event-hook (list (lambda () (setq after clicked)))))
              (urusi--take)
              (should clicked)
              ;; After the handler, so that what it changed can be shown.
              (should after)))
        (urusi-stop)))))

(ert-deftest urusi-call-gets-its-answer ()
  "A call reaches the host, and the answer reaches whoever asked."
  (let ((posted nil)
        (from-host nil)
        (answer 'none))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message) (push message posted) t))
              ((symbol-function 'w32-host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (let* ((id (urusi-call "window.size" nil (lambda (value) (setq answer value))))
             (call (json-parse-string (car posted) :object-type 'plist)))
        (should (equal (plist-get call :type) "call"))
        (should (equal (plist-get call :method) "window.size"))
        ;; No arguments still goes as an object, which is what the host
        ;; reads them as; read back as a plist, that is nothing at all.
        (should (string-match-p "\"args\":{}" (car posted)))
        (push (format "{\"type\":\"reply\",\"id\":%d,\"value\":{\"width\":800,\"height\":600}}" id)
              from-host)
        (urusi--take)
        (should (equal answer '(:width 800 :height 600)))))))

(ert-deftest urusi-call-wait-signals-the-host-error ()
  "An error the host answers with is signalled where the call was made."
  (let ((from-host nil))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message)
                 (let ((id (plist-get (json-parse-string message :object-type 'plist) :id)))
                   (push (format "{\"type\":\"reply\",\"id\":%d,\"error\":\"no such method\"}" id)
                         from-host))
                 t))
              ((symbol-function 'w32-host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (should-error (urusi-call-wait "window.nothing" nil 1)))))

(ert-deftest urusi-host-events-reach-the-hook ()
  "Something that happens to the window reaches the functions waiting for it."
  (let ((from-host (list "{\"type\":\"host-event\",\"event\":\"theme\",\"dark\":true}"))
        (seen nil))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post) (lambda (_) t))
              ((symbol-function 'w32-host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (let ((urusi-host-event-functions
             (list (lambda (event message)
                     (setq seen (list event (plist-get message :dark)))))))
        (urusi--take)
        (should (equal seen '(theme t)))))))

(ert-deftest urusi-sends-only-what-changed ()
  "Send a row again when it changes, and its name alone when it does not."
  (let ((posted nil))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (let ((screen (lambda (second)
                          `(Grid (Rows :key "buffer"
                                       (TextBlock :key "a" "one")
                                       (TextBlock :key "b" ,second))))))
            (urusi-render (funcall screen "two"))
            (urusi-render (funcall screen "two"))
            (urusi-render (funcall screen "three"))
            (setq posted (mapcar (lambda (message)
                                   (json-parse-string message :object-type 'plist))
                                 (nreverse posted)))
            ;; The first time, everything: the XAML around the rows and
            ;; every row in it.
            (should (plist-get (nth 0 posted) :xaml))
            (should (equal (plist-get (aref (plist-get (nth 0 posted) :rows) 0) :items)
                           (vector
                            (list :key "a"
                                  :xaml (concat "<TextBlock" urusi-test--ns ">one</TextBlock>"))
                            (list :key "b"
                                  :xaml (concat "<TextBlock" urusi-test--ns ">two</TextBlock>")))))
            ;; The second, nothing but the names.
            (should-not (plist-get (nth 1 posted) :xaml))
            (should (equal (plist-get (aref (plist-get (nth 1 posted) :rows) 0) :items)
                           [(:key "a") (:key "b")]))
            ;; The third, the row that changed and no more.
            (should (equal (plist-get (aref (plist-get (nth 2 posted) :rows) 0) :items)
                           (vector
                            (list :key "a")
                            (list :key "b"
                                  :xaml (concat "<TextBlock" urusi-test--ns
                                                ">three</TextBlock>"))))))
        (urusi-forget)))))

(ert-deftest urusi-rows-declare-the-namespaces ()
  "A row is read on its own, so it has to bring the namespaces with it."
  (let ((rows (nth 3 (urusi--compile
                      '(Grid (Rows :key "buffer" (TextBlock :key "a" "one")))))))
    (should (equal (cdr (assoc "a" (cdr (assoc "buffer" rows))))
                   (concat "<TextBlock" urusi-test--ns ">one</TextBlock>")))))

(ert-deftest urusi-forgets-when-the-host-is-stale ()
  "Send the whole screen again when the host says it has lost track."
  (let ((posted nil))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (progn
            (urusi-render '(Grid (Rows :key "buffer" (TextBlock :key "a" "one"))))
            (urusi--dispatch '(:type "stale"))
            (setq posted nil)
            (urusi-render '(Grid (Rows :key "buffer" (TextBlock :key "a" "one"))))
            (should (plist-get (json-parse-string (car posted) :object-type 'plist)
                               :xaml)))
        (urusi-forget)))))

(defun urusi-test--nested-screen (outer-width inner-text)
  "A screen with a row of OUTER-WIDTH that has a row saying INNER-TEXT."
  `(Grid (Rows :key "frames" :panel "Canvas"
               (Canvas :key "child" :Width ,outer-width
                       (Rows :key "inside" :panel "Canvas"
                             (TextBlock :key "a" "same")
                             (TextBlock :key "b" ,inner-text))))))

(ert-deftest urusi-rows-inside-a-row ()
  "Rows inside a row go after it, and go whole whenever it does."
  (let ((posted nil))
    (cl-letf (((symbol-function 'w32-host-available-p) (lambda () t))
              ((symbol-function 'w32-host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (cl-flet ((sent (tree)
                      (setq posted nil)
                      (urusi-render tree)
                      (mapcar (lambda (group)
                                (cons (plist-get group :panel)
                                      (mapcar (lambda (item)
                                                (list (plist-get item :key)
                                                      (and (plist-get item :xaml) t)))
                                              (plist-get group :items))))
                              (plist-get (json-parse-string (car posted)
                                                            :object-type 'plist
                                                            :array-type 'list)
                                         :rows))))
            (should (equal (nth 4 (urusi--compile (urusi-test--nested-screen 10 "one")))
                           '(("inside" . ("frames" . "child")))))
            ;; Everything, the outer first.
            (should (equal (sent (urusi-test--nested-screen 10 "one"))
                           '(("frames" ("child" t))
                             ("inside" ("a" t) ("b" t)))))
            ;; Only the inner row that changed.
            (should (equal (sent (urusi-test--nested-screen 10 "two"))
                           '(("frames" ("child" nil))
                             ("inside" ("a" nil) ("b" t)))))
            ;; The outer row is new, so what was inside it is gone.
            (should (equal (sent (urusi-test--nested-screen 20 "two"))
                           '(("frames" ("child" t))
                             ("inside" ("a" t) ("b" t))))))
        (urusi-forget)))))

(ert-deftest urusi-literal-braces ()
  "Text that begins with a brace is text, and not a markup extension."
  (should (equal (urusi-literal "{store.Count}") "{}{store.Count}"))
  (should (equal (urusi-literal "a {b}") "a {b}"))
  (should (equal (car (urusi--compile `(TextBlock :Text ,(urusi-literal "{x}"))))
                 (concat "<TextBlock" urusi-test--ns " Text=\"{}{x}\" />"))))

;;; urusi-test.el ends here
