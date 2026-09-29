;;; urushi-test.el --- Tests for urushi.el  -*- lexical-binding: t; -*-

;; Run with:
;;   emacs -Q --batch -L lisp -l lisp/test/urushi-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'urushi)

(defconst urushi-test--ns
  (concat " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\""
          " xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\""))

(ert-deftest urushi-compile-plain-tree ()
  (let ((result (urushi--compile '(StackPanel :Padding 24
                                             (TextBlock :Text "hi" :FontSize 28)
                                             (Button :IsEnabled nil)))))
    (should (equal (nth 0 result)
                   (concat "<StackPanel" urushi-test--ns " Padding=\"24\">"
                           "<TextBlock Text=\"hi\" FontSize=\"28\" />"
                           "<Button IsEnabled=\"False\" />"
                           "</StackPanel>")))
    (should (null (nth 1 result)))))

(ert-deftest urushi-compile-escapes-attributes-and-text ()
  (let ((xaml (car (urushi--compile '(TextBlock :Text "a<b & \"c\"" "x > y")))))
    (should (equal xaml (concat "<TextBlock" urushi-test--ns
                                " Text=\"a&lt;b &amp; &quot;c&quot;\">x &gt; y</TextBlock>")))))

(ert-deftest urushi-compile-names-elements-with-handlers ()
  (let* ((click (lambda () 'clicked))
         (changed (lambda (args) args))
         (result (urushi--compile
                  `(StackPanel
                    (Button :Content "A" :on-Click ,click)
                    (TextBox :Name "query" :on-TextChanged ,changed)))))
    (should (string-match-p "<Button x:Name=\"urushi1\" Content=\"A\" />" (nth 0 result)))
    (should (string-match-p "<TextBox x:Name=\"query\" />" (nth 0 result)))
    (should (equal (nth 1 result)
                   '((:name "urushi1" :event "Click" :id "urushi1:Click")
                     (:name "query" :event "TextChanged" :id "query:TextChanged"))))
    (should (eq (gethash "urushi1:Click" (nth 2 result)) click))
    (should (eq (gethash "query:TextChanged" (nth 2 result)) changed))))

(ert-deftest urushi-compile-rejects-non-function-handler ()
  (should-error (urushi--compile '(Button :on-Click "not a function"))))

(ert-deftest urushi-round-trip-with-fake-host ()
  "Render through a fake host, then deliver an event back to the handler."
  (let ((posted nil)
        (from-host nil)
        (clicked nil))
    ;; Stand in for the host: these three are what libemacs.dll adds when
    ;; a host application loads it, and are missing in a plain Emacs.
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t))
              ((symbol-function 'host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (unwind-protect
          (progn
            (urushi-start)
            (urushi-render `(Button :Content "OK" :on-Click ,(lambda () (setq clicked t))))
            (setq posted (nreverse posted))
            (let ((hello (json-parse-string (nth 0 posted) :object-type 'plist)))
              (should (equal (plist-get hello :type) "hello"))
              (should (equal (plist-get hello :version) 1))
              ;; Which window system draws the frames, which is how the
              ;; host knows whether they are windows it can post to.
              (should (stringp (plist-get hello :window-system))))
            (let ((screen (json-parse-string (nth 1 posted) :object-type 'plist)))
              (should (equal (plist-get screen :type) "screen"))
              (should (equal (plist-get screen :events)
                             [(:name "urushi1" :event "Click" :id "urushi1:Click")])))
            ;; The host reports the click, with the id it was given.
            (push "{\"type\":\"event\",\"id\":\"urushi1:Click\",\"args\":{}}" from-host)
            (let* ((after nil)
                   (urushi-after-event-hook (list (lambda () (setq after clicked)))))
              (urushi--take)
              (should clicked)
              ;; After the handler, so that what it changed can be shown.
              (should after)))
        (urushi-stop)))))

(defmacro urushi-test--with-fake-host (from-host &rest body)
  "Run BODY with a host that sends whatever is pushed onto FROM-HOST.
The three functions stood in for are what libemacs.dll adds when a host
application loads it, and are missing in a plain Emacs."
  (declare (indent 1))
  `(cl-letf (((symbol-function 'host-available-p) (lambda () t))
             ((symbol-function 'host-post) (lambda (_) t))
             ((symbol-function 'host-take-events)
              (lambda () (prog1 (nreverse ,from-host) (setq ,from-host nil)))))
     ,@body))

;; A handler is free to prompt: closing a tab whose buffer has changes
;; asks whether to throw them away, and the asking is Emacs's own.  C-g
;; and escape leave a prompt by signalling `quit', and that quit used to
;; be let out of `urushi--take', whose `condition-case' took only `error'.
;;
;; On this machine that left the host unread for good.  The timer that
;; reads it was still in `timer-list' with :repeat 0.05, and
;; `timer--triggered' was t: `timer-event-handler' clears that only once
;; its function has returned, and C skips a timer that is still marked
;; triggered.  Nothing read the host again, so every click after that
;; went nowhere while the keyboard went on working, keys being read in C
;; and not through this timer.  Clearing the flag by hand let a queue of
;; clicks through all at once, which is where they had been going.

(ert-deftest urushi-quit-in-a-handler-goes-no-further ()
  "A handler left by C-g is one the rest of the batch survives."
  (let ((from-host nil)
        (after nil)
        (later nil))
    (urushi-test--with-fake-host from-host
      (unwind-protect
          (progn
            (urushi-start)
            (urushi-render
             `(StackPanel
               ;; `signal' rather than a real C-g: a prompt reads with
               ;; `inhibit-quit' nil and signals this itself, and there
               ;; is no keyboard here to press.
               (Button :Content "asks" :on-Click ,(lambda () (signal 'quit nil)))
               (Button :Content "then" :on-Click ,(lambda () (setq later t)))))
            (push "{\"type\":\"event\",\"id\":\"urushi2:Click\",\"args\":{}}" from-host)
            (push "{\"type\":\"event\",\"id\":\"urushi1:Click\",\"args\":{}}" from-host)
            (let ((urushi-after-event-hook (list (lambda () (setq after t)))))
              ;; Returns, rather than signalling the quit onwards.
              (urushi--take))
            ;; The screen is redrawn for the handler that was left, so
            ;; that it shows having been left.
            (should after)
            ;; And what came after it in the same batch is not lost.
            (should later))
        (urushi-stop)))))

;; Bound in C by an Emacs inside the host, and by nothing here, so
;; declared for the `let' below to bind it as that Emacs would.
(defvar host-message-function)

(ert-deftest urushi-starting-asks-to-be-told-of-the-messages ()
  "Starting asks to be told, and stopping gives that up with the looking.
Being told is what the host's messages arrive by; the timer is what is
left for when a telling does not."
  (let ((from-host nil)
        (host-message-function nil))
    (urushi-test--with-fake-host from-host
      (urushi-start)
      (should (eq host-message-function #'urushi--take))
      (urushi-stop)
      (should-not host-message-function))))

(ert-deftest urushi-a-throw-from-a-handler-leaves-the-host-looked-at ()
  "The next look is set even when a handler leaves in a way nothing catches.
A throw is the one that makes the point: no `condition-case' takes it,
so nothing here could have caught up with it after the fact."
  (require 'timer)
  (let ((from-host nil))
    (urushi-test--with-fake-host from-host
      (unwind-protect
          (progn
            (urushi-start)
            (urushi-render
             `(Button :Content "leaves"
                      :on-Click ,(lambda () (throw 'urushi-test-away nil))))
            (push "{\"type\":\"event\",\"id\":\"urushi1:Click\",\"args\":{}}" from-host)
            (catch 'urushi-test-away
              (timer-event-handler urushi--timer))
            (should (memq urushi--timer timer-list))
            (should-not (timer--triggered urushi--timer)))
        (urushi-stop)))))

(ert-deftest urushi-quit-in-a-handler-leaves-the-timer-running ()
  "The host is still looked at after a handler has been left by C-g.
Run through `timer-event-handler', by the name C calls it by, since
what has to hold is what it leaves behind."
  (require 'timer)
  (let ((from-host nil))
    (urushi-test--with-fake-host from-host
      (unwind-protect
          (progn
            (urushi-start)
            (urushi-render
             `(Button :Content "asks" :on-Click ,(lambda () (signal 'quit nil))))
            (push "{\"type\":\"event\",\"id\":\"urushi1:Click\",\"args\":{}}" from-host)
            (timer-event-handler urushi--timer)
            (should (memq urushi--timer timer-list))
            (should-not (timer--triggered urushi--timer)))
        (urushi-stop)))))

(ert-deftest urushi-call-gets-its-answer ()
  "A call reaches the host, and the answer reaches whoever asked."
  (let ((posted nil)
        (from-host nil)
        (answer 'none))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t))
              ((symbol-function 'host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (let* ((id (urushi-call "window.size" nil (lambda (value) (setq answer value))))
             (call (json-parse-string (car posted) :object-type 'plist)))
        (should (equal (plist-get call :type) "call"))
        (should (equal (plist-get call :method) "window.size"))
        ;; No arguments still goes as an object, which is what the host
        ;; reads them as; read back as a plist, that is nothing at all.
        (should (string-match-p "\"args\":{}" (car posted)))
        (push (format "{\"type\":\"reply\",\"id\":%d,\"value\":{\"width\":800,\"height\":600}}" id)
              from-host)
        (urushi--take)
        (should (equal answer '(:width 800 :height 600)))))))

(ert-deftest urushi-call-wait-signals-the-host-error ()
  "An error the host answers with is signalled where the call was made."
  (let ((from-host nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message)
                 (let ((id (plist-get (json-parse-string message :object-type 'plist) :id)))
                   (push (format "{\"type\":\"reply\",\"id\":%d,\"error\":\"no such method\"}" id)
                         from-host))
                 t))
              ((symbol-function 'host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (should-error (urushi-call-wait "window.nothing" nil 1)))))

(ert-deftest urushi-host-events-reach-the-hook ()
  "Something that happens to the window reaches the functions waiting for it."
  (let ((from-host (list "{\"type\":\"host-event\",\"event\":\"theme\",\"dark\":true}"))
        (seen nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post) (lambda (_) t))
              ((symbol-function 'host-take-events)
               (lambda () (prog1 (nreverse from-host) (setq from-host nil)))))
      (let ((urushi-host-event-functions
             (list (lambda (event message)
                     (setq seen (list event (plist-get message :dark)))))))
        (urushi--take)
        (should (equal seen '(theme t)))))))

(ert-deftest urushi-sends-only-what-changed ()
  "Send a row again when it changes, and its name alone when it does not."
  (let ((posted nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (let ((screen (lambda (second)
                          `(Grid (Rows :key "buffer"
                                       (TextBlock :key "a" "one")
                                       (TextBlock :key "b" ,second))))))
            (urushi-render (funcall screen "two"))
            (urushi-render (funcall screen "two"))
            (urushi-render (funcall screen "three"))
            (setq posted (mapcar (lambda (message)
                                   (json-parse-string message :object-type 'plist))
                                 (nreverse posted)))
            ;; The first time, everything: the XAML around the rows and
            ;; every row in it.
            (should (plist-get (nth 0 posted) :xaml))
            (should (equal (plist-get (aref (plist-get (nth 0 posted) :rows) 0) :items)
                           (vector
                            (list :key "a"
                                  :xaml (concat "<TextBlock" urushi-test--ns ">one</TextBlock>"))
                            (list :key "b"
                                  :xaml (concat "<TextBlock" urushi-test--ns ">two</TextBlock>")))))
            ;; The second, nothing but the names.
            (should-not (plist-get (nth 1 posted) :xaml))
            (should (equal (plist-get (aref (plist-get (nth 1 posted) :rows) 0) :items)
                           [(:key "a") (:key "b")]))
            ;; The third, the row that changed and no more.
            (should (equal (plist-get (aref (plist-get (nth 2 posted) :rows) 0) :items)
                           (vector
                            (list :key "a")
                            (list :key "b"
                                  :xaml (concat "<TextBlock" urushi-test--ns
                                                ">three</TextBlock>"))))))
        (urushi-forget)))))

(ert-deftest urushi-rows-declare-the-namespaces ()
  "A row is read on its own, so it has to bring the namespaces with it."
  (let ((rows (nth 3 (urushi--compile
                      '(Grid (Rows :key "buffer" (TextBlock :key "a" "one")))))))
    (should (equal (cadr (assoc "a" (cdr (assoc "buffer" rows))))
                   (concat "<TextBlock" urushi-test--ns ">one</TextBlock>")))))

(ert-deftest urushi-rows-bring-their-events ()
  "A row brings the events of what is in it, by ids that say where it is."
  (let ((posted nil)
        (clicked nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (let ((screen (lambda (label)
                          `(Grid (Button :Content "outside" :on-Click ,#'ignore)
                                 (Rows :key "tabs"
                                       (Button :key "a" :Content ,label
                                               :on-Click ,(lambda () (setq clicked label))))))))
            (urushi-render (funcall screen "one"))
            (let* ((screen (json-parse-string (car posted) :object-type 'plist
                                              :array-type 'list))
                   (row (car (plist-get (car (plist-get screen :rows)) :items))))
              ;; The XAML around the rows has its own, and the row its own,
              ;; each named in its own scope.
              (should (equal (plist-get screen :events)
                             '((:name "urushi1" :event "Click" :id "urushi1:Click"))))
              (should (equal (plist-get row :events)
                             '((:name "urushi1" :event "Click" :id "tabs/a/urushi1:Click")))))
            ;; Kept as it was, and still reaching the handler of this screen.
            (setq posted nil)
            (urushi-render (funcall screen "one"))
            (should (equal (plist-get (json-parse-string (car posted) :object-type 'plist
                                                         :array-type 'list)
                                      :rows)
                           '((:panel "tabs" :items ((:key "a"))))))
            (urushi--call-handler "tabs/a/urushi1:Click" nil)
            (should (equal clicked "one")))
        (urushi-forget)))))

(ert-deftest urushi-forgets-when-the-host-is-stale ()
  "Send the whole screen again when the host says it has lost track."
  (let ((posted nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (progn
            (urushi-render '(Grid (Rows :key "buffer" (TextBlock :key "a" "one"))))
            (urushi--dispatch '(:type "stale"))
            (setq posted nil)
            (urushi-render '(Grid (Rows :key "buffer" (TextBlock :key "a" "one"))))
            (should (plist-get (json-parse-string (car posted) :object-type 'plist)
                               :xaml)))
        (urushi-forget)))))

(defun urushi-test--nested-screen (outer-width inner-text)
  "A screen with a row of OUTER-WIDTH that has a row saying INNER-TEXT."
  `(Grid (Rows :key "frames" :panel "Canvas"
               (Canvas :key "child" :Width ,outer-width
                       (Rows :key "inside" :panel "Canvas"
                             (TextBlock :key "a" "same")
                             (TextBlock :key "b" ,inner-text))))))

(ert-deftest urushi-rows-inside-a-row ()
  "Rows inside a row go after it, and go whole whenever it does."
  (let ((posted nil))
    (cl-letf (((symbol-function 'host-available-p) (lambda () t))
              ((symbol-function 'host-post)
               (lambda (message) (push message posted) t)))
      (unwind-protect
          (cl-flet ((sent (tree)
                      (setq posted nil)
                      (urushi-render tree)
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
            (should (equal (nth 4 (urushi--compile (urushi-test--nested-screen 10 "one")))
                           '(("inside" . ("frames" . "child")))))
            ;; Everything, the outer first.
            (should (equal (sent (urushi-test--nested-screen 10 "one"))
                           '(("frames" ("child" t))
                             ("inside" ("a" t) ("b" t)))))
            ;; Only the inner row that changed.
            (should (equal (sent (urushi-test--nested-screen 10 "two"))
                           '(("frames" ("child" nil))
                             ("inside" ("a" nil) ("b" t)))))
            ;; The outer row is new, so what was inside it is gone.
            (should (equal (sent (urushi-test--nested-screen 20 "two"))
                           '(("frames" ("child" t))
                             ("inside" ("a" t) ("b" t))))))
        (urushi-forget)))))

(ert-deftest urushi-literal-braces ()
  "Text that begins with a brace is text, and not a markup extension."
  (should (equal (urushi-literal "{store.Count}") "{}{store.Count}"))
  (should (equal (urushi-literal "a {b}") "a {b}"))
  (should (equal (car (urushi--compile `(TextBlock :Text ,(urushi-literal "{x}"))))
                 (concat "<TextBlock" urushi-test--ns " Text=\"{}{x}\" />"))))

;;; urushi-test.el ends here
