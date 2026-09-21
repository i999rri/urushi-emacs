;;; urusi.el --- Native WinUI 3 UI built from Emacs Lisp  -*- lexical-binding: t; -*-

;;; Commentary:

;; Builds the window of the urusi-emacs host, a WinUI 3 application that
;; loads Emacs into its own process.  Messages go by calling the host,
;; with `w32-host-post' and `w32-host-take-events'; each one is a line of
;; JSON.
;;
;; A UI is a tree of s-expressions:
;;
;;   (Tag :Property value ... child ...)
;;
;; which is turned into XAML and loaded by the host.  Strings are text
;; content.  Properties whose name starts with `on-' are event handlers,
;; called with a plist of event arguments (or with none, if the handler
;; takes none):
;;
;;   (urusi-render
;;    `(StackPanel :Padding 24
;;       (Button :Content "OK" :on-Click ,(lambda () (message "clicked")))))
;;
;; Supported events: Click (buttons), TextChanged (:text), SelectionChanged
;; (:index), Toggled (:on).

;;; Code:

(require 'cl-lib)
(require 'subr-x)

(declare-function w32-host-available-p "w32host.c")
(declare-function w32-host-post "w32host.c" (message))
(declare-function w32-host-take-events "w32host.c")

(defgroup urusi nil
  "Native WinUI 3 UI built from Emacs Lisp."
  :group 'environment)

(defcustom urusi-poll-interval 0.05
  "How often to look for messages from the host, in seconds.
The host sends from a thread of its own, which cannot run Lisp, so its
messages wait in a queue until Emacs looks at it."
  :type 'number)

(defvar urusi--timer nil
  "Timer that looks for messages from the host.")

(defvar urusi--handlers (make-hash-table :test #'eql)
  "Event handlers of the UI currently shown, keyed by event id.")

(defconst urusi--namespaces
  (concat " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\""
          " xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"")
  "Namespace declarations the root element needs for XamlReader.Load.")

;;;; Talking to the host

(defun urusi-available-p ()
  "Return non-nil if this Emacs runs inside the urusi-emacs host."
  (and (fboundp 'w32-host-available-p) (w32-host-available-p)))

(defun urusi-start ()
  "Start talking to the urusi-emacs host."
  (interactive)
  (unless (urusi-available-p)
    (user-error "urusi: This Emacs does not run inside the host"))
  (urusi-stop)
  ;; The frame is a child window of the host's, which Windows draws no
  ;; menu bar above.
  (menu-bar-mode -1)
  (tool-bar-mode -1)
  (setq urusi--timer
        (run-with-timer urusi-poll-interval urusi-poll-interval #'urusi--take))
  (urusi--send '(:type "hello" :version 1)))

(defun urusi-stop ()
  "Stop looking for messages from the host."
  (interactive)
  (when urusi--timer
    (cancel-timer urusi--timer)
    (setq urusi--timer nil)))

(defun urusi--send (message)
  "Send MESSAGE, a plist, to the host as one JSON object."
  (unless (w32-host-post (json-serialize message))
    (user-error "urusi: This Emacs does not run inside the host")))

(defun urusi--take ()
  "Handle the messages the host has sent since the last look."
  (dolist (message (w32-host-take-events))
    (condition-case err
        (urusi--dispatch (json-parse-string message
                                            :object-type 'plist
                                            :false-object nil
                                            :null-object nil))
      ;; The echo area is drawn by whatever this was on its way to.
      (error (urusi--log "%S in %s" err message)))))

(defun urusi--log (format &rest arguments)
  "Write FORMAT with ARGUMENTS where the host will show it.
The echo area is no use for what goes wrong on the way to drawing it.
Neither is the standard error: by the time a host loads Emacs, its C
runtime has taken the handles the process started with, and what Emacs
writes there goes nowhere.  So this asks the host, the way everything
else does."
  (let ((text (apply #'format format arguments)))
    (if (urusi-available-p)
        (w32-host-post (json-serialize (list :type "log" :text text)))
      (princ (concat "urusi: " text "\n") #'external-debugging-output))))

(defvar urusi-scale 1.0
  "How many pixels of the screen go to one of the pixels XAML counts in.
Emacs measures in the pixels of the screen, XAML in 96ths of an inch,
and on a display that is scaled the two are not the same.  The host
says which it is when it answers the first message.")

(defvar urusi-message-hook nil
  "Functions to run with a message from the host this file does not know.
Each takes the message, as a plist.")

(defvar urusi-stale-hook nil
  "Functions to run when the host has lost track of what it shows.
Whatever is drawing has to draw the whole of it again.")

(defvar urusi--seen nil
  "Kinds of message the host has sent, so each is only remarked on once.")

(defun urusi--dispatch (message)
  "Handle MESSAGE, a plist parsed from the host."
  (let ((type (plist-get message :type)))
    (unless (member type urusi--seen)
      (push type urusi--seen)
      (urusi--log "first %s from the host" type)))
  (pcase (plist-get message :type)
    ("hello"
     (setq urusi-scale (or (plist-get message :scale) 1.0))
     (message "urusi: Talking to %s" (plist-get message :host)))
    ("event" (urusi--call-handler (plist-get message :id) (plist-get message :args)))
    ("reply" (urusi--reply message))
    ("host-event"
     (run-hook-with-args 'urusi-host-event-functions
                         (intern (plist-get message :event)) message))
    ("stale" (urusi-forget) (run-hooks 'urusi-stale-hook))
    ("error" (message "urusi: %s" (plist-get message :message)))
    (_ (run-hook-with-args 'urusi-message-hook message))))

;;;; Asking the host

(defvar urusi-host-event-functions nil
  "Functions to run when something happens to the host window.
Each is called with the event, a symbol, and the message it came in,
a plist.  The events are `activated' and `deactivated', when the window
gains and loses the focus, and `theme', with :dark, when Windows
switches between light and dark.")

(defvar urusi--calls (make-hash-table :test #'eql)
  "What to do with the answer to each call still waiting for one.
The value is a function of the value and the error, one of which is
nil.")

(defvar urusi--next-call 0
  "The number of the last call made.")

(defun urusi-call (method &optional args callback)
  "Ask the host to do METHOD with ARGS, a plist, and return at once.
CALLBACK, if given, is called with the value the host answers with once
it has; an error is logged.  Return the number of the call.

Some methods wait on the person using the application, as
\"dialog.open-file\" does, and answer when they have: Emacs goes on in
the meantime, which is why the answer comes to a callback.

The methods:

  window.title     :title        what the window is called
  window.state     :state        \"normal\", \"maximized\", \"minimized\",
                                 or \"fullscreen\"
  window.topmost   :on           whether it stays above other windows
  window.size                    its size in the pixels of the screen,
                                 as (:width W :height H)
  window.resize    :width :height
  window.theme                   whether it is drawn dark, as (:dark B)
  dialog.open-file               the file the person chose, or nil"
  (let ((id (cl-incf urusi--next-call)))
    (puthash id
             (lambda (value error)
               (if error
                   (urusi--log "%s: %s" method error)
                 (when callback
                   (funcall callback value))))
             urusi--calls)
    (urusi--send (list :type "call" :id id :method method
                       ;; An empty plist is not an object to JSON.
                       :args (or args (make-hash-table))))
    id))

(defun urusi-call-wait (method &optional args timeout)
  "Ask the host to do METHOD with ARGS, a plist, and return its answer.
Wait no more than TIMEOUT seconds, 5 by default, and signal an error if
the host answers with one or does not answer.  Use `urusi-call' for a
method that waits on the person using the application."
  (let* ((done nil)
         (answer nil)
         (failure nil)
         (id (cl-incf urusi--next-call))
         (deadline (+ (float-time) (or timeout 5))))
    (puthash id
             (lambda (value error)
               (setq done t answer value failure error))
             urusi--calls)
    (urusi--send (list :type "call" :id id :method method
                       :args (or args (make-hash-table))))
    ;; The answer comes the way everything from the host does, so look
    ;; for it here rather than wait for the timer to.
    (while (and (not done) (< (float-time) deadline))
      (sleep-for 0.01)
      (urusi--take))
    (remhash id urusi--calls)
    (cond (failure (error "urusi: %s: %s" method failure))
          ((not done) (error "urusi: %s: no answer from the host" method))
          (t answer))))

(defun urusi--reply (message)
  "Pass the answer in MESSAGE to whatever is waiting for it."
  ;; The host sends numbers as JSON numbers, which may come back as floats.
  (let* ((id (truncate (plist-get message :id)))
         (waiting (gethash id urusi--calls)))
    (when waiting
      (remhash id urusi--calls)
      (funcall waiting (plist-get message :value) (plist-get message :error)))))

(defun urusi--call-handler (id args)
  "Call the handler registered for event ID with ARGS."
  ;; The host sends numbers as JSON numbers, which may come back as floats.
  (when-let* ((handler (gethash (truncate id) urusi--handlers)))
    (condition-case err
        (if (eql (cdr (func-arity handler)) 0)
            (funcall handler)
          (funcall handler args))
      (error (message "urusi: Handler failed: %S" err)))))

;;;; Tree to XAML

(defvar urusi--row-xaml (make-hash-table :test #'eq)
  "What each row of the last screen compiled to, keyed by the row itself.
A screen is mostly the screen before it, and compiling a row that has
not changed arrives at the string that is already here.  It holds the
last screen only: what a screen does not use is what the next one drops.")

(defun urusi--escape (text attribute)
  "Escape TEXT for XAML, as an ATTRIBUTE value if non-nil."
  (let ((escaped (replace-regexp-in-string "&" "&amp;" text t t)))
    (setq escaped (replace-regexp-in-string "<" "&lt;" escaped t t))
    (setq escaped (replace-regexp-in-string ">" "&gt;" escaped t t))
    (if attribute
        (replace-regexp-in-string "\"" "&quot;" escaped t t)
      escaped)))

(defun urusi--value (value)
  "Return VALUE as the string XAML expects for a property."
  (cond ((stringp value) value)
        ((eq value t) "True")
        ((null value) "False")
        ((numberp value) (number-to-string value))
        ((symbolp value) (symbol-name value))
        (t (error "urusi: Cannot use %S as a property value" value))))

(defun urusi--compile (tree)
  "Turn TREE into XAML.
Return (XAML EVENTS HANDLERS ROWS).

EVENTS is the list the host uses to attach events, HANDLERS maps event
ids to functions.

ROWS is what the host is to keep between one screen and the next.  A
node written as

  (Rows :key NAME CHILD...)

becomes an empty panel in the XAML, named after NAME, and its children
are compiled one by one into ROWS as (NAME . ((KEY . XAML)...)).  Each
child has to carry a :key of its own, which is what the host reuses it
by: a child whose XAML has not changed is left alone, wherever it has
moved to."
  (let ((next-name 0)
        (next-id 0)
        (events nil)
        (rows nil)
        (was urusi--row-xaml)
        (handlers (make-hash-table :test #'eql)))
    (setq urusi--row-xaml (make-hash-table :test #'eq))
    (cl-labels
        ((row-xaml (child)
           ;; A row built out of the same thing twice is the same row,
           ;; and whoever built it says so by handing back the very
           ;; object it handed back last time.  Compiling it again
           ;; would only arrive at the string that is already here.
           (or (gethash child was)
               (let ((before events)
                     ;; A row is read on its own, so it declares the
                     ;; namespaces itself.
                     (xaml (node child t)))
                 ;; A row with a handler in it is compiled every time:
                 ;; the handler is registered as it is compiled, and
                 ;; skipping that would leave it unreachable.
                 (if (eq before events)
                     (puthash child xaml urusi--row-xaml)
                   xaml))))
         (node (form root)
           (cond
            ((stringp form) (urusi--escape form nil))
            ((eq (car-safe form) 'Rows)
             (let ((rest (cdr form))
                   (properties nil)
                   (attributes nil)
                   (name nil))
               (while (keywordp (car rest))
                 (push (cons (substring (symbol-name (pop rest)) 1) (pop rest))
                       properties))
               (setq properties (nreverse properties))
               (setq name (urusi--value
                           (or (cdr (assoc "key" properties))
                               (error "urusi: Rows needs a :key: %S" form))))
               (dolist (property properties)
                 (unless (member (car property) '("key" "panel"))
                   (push (format " %s=\"%s\"" (car property)
                                 (urusi--escape (urusi--value (cdr property)) t))
                         attributes)))
               (push (cons name
                           (mapcar (lambda (child)
                                     (cons (urusi--row-key child)
                                           (row-xaml child)))
                                   rest))
                     rows)
               ;; :panel says what holds the rows.  A StackPanel puts
               ;; each under the last, which is what a list of things
               ;; wants; a Canvas puts each where it says it goes,
               ;; which is what a screen wants.
               (format "<%s x:Name=\"%s\"%s />"
                       (or (cdr (assoc "panel" properties)) "StackPanel")
                       (urusi--escape name t)
                       (apply #'concat (nreverse attributes)))))
            ((and (consp form) (symbolp (car form)))
             (let ((tag (symbol-name (car form)))
                   (rest (cdr form))
                   (attributes nil)
                   (element-handlers nil)
                   (name nil))
               (while (keywordp (car rest))
                 (let ((key (substring (symbol-name (pop rest)) 1))
                       (value (pop rest)))
                   (cond
                    ((string-prefix-p "on-" key)
                     (unless (functionp value)
                       (error "urusi: %s handler is not a function: %S" key value))
                     (push (cons (substring key 3) value) element-handlers))
                    ((string= key "Name")
                     (setq name (urusi--value value)))
                    ;; A key says which element this is between one
                    ;; screen and the next, and is not XAML's business.
                    ((string= key "key"))
                    (t
                     (push (format " %s=\"%s\"" key (urusi--escape (urusi--value value) t))
                           attributes)))))
               ;; The host finds elements by name to attach events.
               (when (and element-handlers (not name))
                 (setq name (format "urusi%d" (cl-incf next-name))))
               (dolist (handler (nreverse element-handlers))
                 (let ((id (cl-incf next-id)))
                   (puthash id (cdr handler) handlers)
                   (push (list :name name :event (car handler) :id id) events)))
               (let ((open (concat "<" tag
                                   (and root urusi--namespaces)
                                   (and name (format " x:Name=\"%s\"" (urusi--escape name t)))
                                   (apply #'concat (nreverse attributes))))
                     (children (mapconcat (lambda (child) (node child nil)) rest "")))
                 (if (string-empty-p children)
                     (concat open " />")
                   (concat open ">" children "</" tag ">")))))
            (t (error "urusi: Cannot render %S" form)))))
      (let ((xaml (node tree t)))
        (list xaml (nreverse events) handlers (nreverse rows))))))

(defun urusi--row-key (form)
  "Return the :key of FORM, which a child of `Rows' must have."
  (let ((key (plist-get (cdr-safe form) :key)))
    (unless key
      (error "urusi: A row needs a :key: %S" form))
    (urusi--value key)))

;;;; Rendering

(defvar urusi--shown nil
  "What the host was last sent, as (XAML . ROWS), or nil for nothing.
It is what a screen is compared against to find what has changed.")

(defun urusi-render (tree)
  "Show TREE, a UI written as s-expressions, in the host window.

Only what has changed since the last call is sent.  The XAML around the
rows goes when it differs from last time, and with it every row; a row
goes when its own XAML differs.  Rows that have not changed are named
and nothing more, and the host leaves the elements it has for them
alone, so that what they were doing they go on doing.

Returns what was sent, in words, which is worth having when the screen
is slower than it should be."
  (pcase-let* ((`(,xaml ,events ,handlers ,rows) (urusi--compile tree))
               (`(,shown-xaml . ,shown-rows) urusi--shown)
               (same-chrome (equal xaml shown-xaml))
               (changed 0)
               (total 0))
    (urusi--send
     (nconc (list :type "screen")
            (unless same-chrome (list :xaml xaml :events (vconcat events)))
            (list :rows
                  (vconcat
                   (mapcar
                    (lambda (group)
                      (let ((shown (and same-chrome (cdr (assoc (car group) shown-rows)))))
                        (list :panel (car group)
                              :items
                              (vconcat
                               (mapcar
                                (lambda (row)
                                  (cl-incf total)
                                  (if (equal (cdr row) (cdr (assoc (car row) shown)))
                                      (list :key (car row))
                                    (cl-incf changed)
                                    (list :key (car row) :xaml (cdr row))))
                                (cdr group))))))
                    rows)))))
    (setq urusi--shown (cons xaml rows))
    (setq urusi--handlers handlers)
    (format "%s, %d/%d rows" (if same-chrome "same chrome" "NEW CHROME")
            changed total)))

(defun urusi-forget ()
  "Forget what the host is showing, so that the next screen is sent whole."
  (setq urusi--shown nil)
  (setq urusi--row-xaml (make-hash-table :test #'eq)))

(defun urusi-demo ()
  "Show a small UI in the host, to check that everything is connected."
  (interactive)
  (unless urusi--timer
    (urusi-start))
  (let ((count 0))
    (urusi-render
     `(StackPanel :Padding 24 :Spacing 12
                  (TextBlock :Text "urusi-emacs" :FontSize 28)
                  (TextBox :PlaceholderText "Type something"
                           :on-TextChanged ,(lambda (args)
                                              (message "urusi: Text is %S" (plist-get args :text))))
                  (Button :Content "Click me"
                          :on-Click ,(lambda ()
                                       (setq count (1+ count))
                                       (message "urusi: Clicked %d times" count)))))))

(provide 'urusi)
;;; urusi.el ends here
