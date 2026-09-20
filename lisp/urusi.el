;;; urusi.el --- Native WinUI 3 UI built from Emacs Lisp  -*- lexical-binding: t; -*-

;;; Commentary:

;; Talks to the urusi-emacs host, a WinUI 3 app, over a TCP connection on the
;; loopback interface, one JSON message per line.
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

(defgroup urusi nil
  "Native WinUI 3 UI built from Emacs Lisp."
  :group 'environment)

(defcustom urusi-port 7680
  "Port the urusi-emacs host listens on, on 127.0.0.1."
  :type 'integer)

(defvar urusi--process nil
  "Connection to the host.")

(defvar urusi--pending ""
  "Output from the host that does not end in a newline yet.")

(defvar urusi--handlers (make-hash-table :test #'eql)
  "Event handlers of the UI currently shown, keyed by event id.")

(defconst urusi--namespaces
  (concat " xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\""
          " xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"")
  "Namespace declarations the root element needs for XamlReader.Load.")

;;;; Connection

(defun urusi-connect ()
  "Connect to the urusi-emacs host."
  (interactive)
  (urusi-disconnect)
  (setq urusi--pending "")
  (setq urusi--process
        (make-network-process :name "urusi"
                              :host "127.0.0.1"
                              :service urusi-port
                              :coding 'utf-8-unix
                              :noquery t
                              :filter #'urusi--filter
                              :sentinel #'urusi--sentinel))
  (urusi--send '(:type "hello" :version 1)))

(defun urusi-disconnect ()
  "Disconnect from the urusi-emacs host."
  (interactive)
  (when (process-live-p urusi--process)
    (delete-process urusi--process))
  (setq urusi--process nil))

(defun urusi--send (message)
  "Send MESSAGE, a plist, to the host as one line of JSON."
  (unless (process-live-p urusi--process)
    (user-error "urusi: Not connected (M-x urusi-connect)"))
  (process-send-string urusi--process (concat (json-serialize message) "\n")))

(defun urusi--filter (_process output)
  "Split OUTPUT from the host into lines and handle each message."
  (setq urusi--pending (concat urusi--pending output))
  (let (newline)
    (while (setq newline (string-search "\n" urusi--pending))
      (let ((line (substring urusi--pending 0 newline)))
        (setq urusi--pending (substring urusi--pending (1+ newline)))
        (unless (string-empty-p line)
          (urusi--dispatch (json-parse-string line
                                              :object-type 'plist
                                              :false-object nil
                                              :null-object nil)))))))

(defun urusi--sentinel (_process event)
  "Report that the connection ended, as described by EVENT."
  (unless (string-prefix-p "open" event)
    (message "urusi: %s" (string-trim event))))

(defun urusi--dispatch (message)
  "Handle MESSAGE, a plist parsed from the host."
  (pcase (plist-get message :type)
    ("hello" (message "urusi: Connected to %s" (plist-get message :host)))
    ("event" (urusi--call-handler (plist-get message :id) (plist-get message :args)))
    ("error" (message "urusi: %s" (plist-get message :message)))))

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
Return (XAML EVENTS HANDLERS), where EVENTS is the list the host uses to
attach events and HANDLERS maps event ids to functions."
  (let ((next-name 0)
        (next-id 0)
        (events nil)
        (handlers (make-hash-table :test #'eql)))
    (cl-labels
        ((node (form root)
           (cond
            ((stringp form) (urusi--escape form nil))
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
      (list (node tree t) (nreverse events) handlers))))

;;;; Rendering

(defun urusi-render (tree)
  "Show TREE, a UI written as s-expressions, in the host window."
  (pcase-let ((`(,xaml ,events ,handlers) (urusi--compile tree)))
    (urusi--send (list :type "render" :xaml xaml :events (vconcat events)))
    (setq urusi--handlers handlers)))

(defun urusi-demo ()
  "Show a small UI in the host, to check that everything is connected."
  (interactive)
  (unless (process-live-p urusi--process)
    (urusi-connect))
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
