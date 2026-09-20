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
  (let* ((received nil)
         (host-connection nil)
         (server (make-network-process
                  :name "urusi-fake-host" :server t :host "127.0.0.1" :service t
                  :coding 'utf-8-unix :noquery t
                  :log (lambda (_server connection _message)
                         (setq host-connection connection))
                  :filter (lambda (_process output)
                            (setq received (concat received output)))))
         (urusi-port (process-contact server :service))
         (clicked nil))
    (unwind-protect
        (progn
          (urusi-connect)
          (urusi-render `(Button :Content "OK" :on-Click ,(lambda () (setq clicked t))))
          ;; Wait until the fake host has both lines: hello and render.
          (with-timeout (5 (error "Fake host received %S" received))
            (while (< (cl-count ?\n (or received "")) 2)
              (accept-process-output nil 0.05)))
          (let ((lines (split-string received "\n" t)))
            (should (equal (json-parse-string (nth 0 lines) :object-type 'plist)
                           '(:type "hello" :version 1)))
            (let ((render (json-parse-string (nth 1 lines) :object-type 'plist)))
              (should (equal (plist-get render :type) "render"))
              (should (equal (plist-get render :events)
                             [(:name "urusi1" :event "Click" :id 1)]))))
          ;; The host reports the click; the id arrives as a JSON number.
          (process-send-string host-connection
                               "{\"type\":\"event\",\"id\":1,\"args\":{}}\n")
          (with-timeout (5 (error "Handler was not called"))
            (while (not clicked)
              (accept-process-output nil 0.05)))
          (should clicked))
      (urusi-disconnect)
      (delete-process server))))

;;; urusi-test.el ends here
