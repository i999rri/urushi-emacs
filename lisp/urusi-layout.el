;;; urusi-layout.el --- Panels laid out around the Emacs frame  -*- lexical-binding: t; -*-

;;; Commentary:

;; A layout is a tree of panels, written in Lisp and changed from Lisp,
;; that fills the window: the Emacs frame is one panel of it, and what
;; else is on the screen, native controls or nothing at all, are the
;; others.
;;
;;   (setq urusi-layout
;;         '(row
;;           (panel :id explorer :size 260 :content my-file-tree)
;;           (column
;;            (panel :id editor :content emacs)
;;            (panel :id output :size 200 :hidden t :content my-output))))
;;
;;   (setq urusi-screen-components '(my-titlebar urusi-layout-component))
;;
;; A `row' puts what is in it side by side and a `column' one above the
;; next.  Each part of either has a :size, in the pixels XAML counts in,
;; or shares what the parts with none leave, in proportion to its
;; :weight, which is 1 unless it says otherwise.  A part can have an :id,
;; which is how it is shown, hidden and resized once the layout is on
;; the screen:
;;
;;   `urusi-layout-toggle'  `urusi-layout-show'  `urusi-layout-hide'
;;   `urusi-layout-resize'
;;
;; A part that is hidden is not there at all, and the ones beside it
;; share its room; one whose parts are all hidden is hidden with them.
;; Between two parts that are shown is a splitter, which the host lets
;; be dragged, and where it is let go is the size remembered.
;;
;; What a panel holds is its :content:
;;
;;   emacs       the Emacs frame, windows and all; there is to be one
;;   nil         nothing, a space for the sake of a space
;;   a function  called with the frame, returning a tree for `urusi-render'
;;   a tree      put there as it is
;;
;; and :background, :padding, :margin and :corner-radius, if it has
;; them, are the panel's own.

;;; Code:

(require 'cl-lib)
(require 'urusi)
(require 'urusi-screen)

(defgroup urusi-layout nil
  "Panels laid out around the Emacs frame."
  :group 'urusi)

(defcustom urusi-layout '(panel :id editor :content emacs)
  "The panels the screen is laid out in; see the commentary of urusi-layout.el.
The one there is at first is the Emacs frame and nothing else."
  :type 'sexp)

(defcustom urusi-layout-splitter-size 4
  "How wide a splitter between two parts is, in the pixels XAML counts in."
  :type 'number)

(defcustom urusi-layout-minimum-size 40
  "How small the host lets a splitter make a part, in the same pixels."
  :type 'number)

(defvar urusi-layout--state (make-hash-table :test #'eq)
  "What has been changed about each part since the layout was written.
Keyed by :id, each is a plist of :hidden and :size.  The layout as it is
written is left alone, so that setting it again starts from it.")

;;;; Reading the tree

(defun urusi-layout--kind (node)
  "Return what NODE is: `row', `column' or `panel'."
  (let ((kind (car-safe node)))
    (unless (memq kind '(row column panel))
      (error "urusi-layout: Not a row, a column or a panel: %S" node))
    kind))

(defun urusi-layout--properties (node)
  "Return the properties NODE is written with, a plist."
  (let ((rest (cdr node))
        (properties nil))
    (while (keywordp (car rest))
      (setq properties (append properties (list (pop rest) (pop rest)))))
    properties))

(defun urusi-layout--children (node)
  "Return the parts of NODE, a row or a column."
  (let ((rest (cdr node)))
    (while (keywordp (car rest))
      (setq rest (cddr rest)))
    rest))

(defun urusi-layout--get (node property)
  "Return PROPERTY of NODE, as changed since, or as it is written.
The state is kept by :id, so a part with none is as it is written."
  (let* ((properties (urusi-layout--properties node))
         (id (plist-get properties :id))
         (state (and id (gethash id urusi-layout--state))))
    (if (and state (plist-member state property))
        (plist-get state property)
      (plist-get properties property))))

(defun urusi-layout--shown-p (node)
  "Return non-nil if NODE is on the screen.
A row or a column is, unless it is hidden or all its parts are."
  (and (not (urusi-layout--get node :hidden))
       (or (eq (urusi-layout--kind node) 'panel)
           (cl-some #'urusi-layout--shown-p (urusi-layout--children node)))))

(defun urusi-layout--find (id &optional node)
  "Return the part of NODE whose :id is ID, or nil if there is none.
NODE defaults to the whole of `urusi-layout'."
  (let ((node (or node urusi-layout)))
    (if (eq (plist-get (urusi-layout--properties node) :id) id)
        node
      (and (not (eq (urusi-layout--kind node) 'panel))
           (cl-some (lambda (child) (urusi-layout--find id child))
                    (urusi-layout--children node))))))

;;;; Building it

(defun urusi-layout--length (node)
  "Return how much room NODE takes along its parent, as XAML writes it."
  (let ((size (urusi-layout--get node :size)))
    (if (numberp size)
        (number-to-string size)
      (format "%s*" (or (urusi-layout--get node :weight) 1)))))

(defun urusi-layout--id-name (node)
  "Return the :id of NODE as it goes in a name, or \"-\" if it has none."
  (let ((id (plist-get (urusi-layout--properties node) :id)))
    (if id (symbol-name id) "-")))

(defun urusi-layout--content (content frame)
  "Return CONTENT, what a panel holds, as a tree, for FRAME."
  (cond
   ((null content) '(Border))
   ((eq content 'emacs) (urusi-screen-frame-site frame))
   ((functionp content) (funcall content frame))
   ((and (consp content) (symbolp (car content))) content)
   (t (error "urusi-layout: Cannot put %S in a panel" content))))

(defun urusi-layout--panel (node frame)
  "Return NODE, a panel, as a tree, for FRAME."
  (let ((properties (urusi-layout--properties node)))
    `(Border ,@(cl-loop for (key xaml) in '((:background :Background)
                                            (:padding :Padding)
                                            (:margin :Margin)
                                            (:corner-radius :CornerRadius))
                        for value = (plist-get properties key)
                        when value append (list xaml value))
             ,(urusi-layout--content (plist-get properties :content) frame))))

(defun urusi-layout--splitter (horizontal before after position)
  "Return the splitter between BEFORE and AFTER, at POSITION of their grid.
HORIZONTAL is non-nil if it is between parts side by side.

Its name says which way it goes and what is either side of it, which is
what the host needs to drag it and to say where it was let go."
  `(Border :Name ,(format "urusi-splitter:%s:%s:%s"
                          (if horizontal "h" "v")
                          (urusi-layout--id-name before)
                          (urusi-layout--id-name after))
           ,(if horizontal :Grid.Column :Grid.Row) ,position
           :Background "Transparent"))

(defun urusi-layout--in-cell (tree horizontal position)
  "Return TREE placed at POSITION of the grid around it.
HORIZONTAL is non-nil if the grid puts its parts side by side."
  (cons (car tree)
        (append (list (if horizontal :Grid.Column :Grid.Row) position)
                (cdr tree))))

(defun urusi-layout--group (node frame)
  "Return NODE, a row or a column, as a grid, for FRAME.
Each part it shows has a cell of its own, and between two of them is a
splitter in a cell of its own."
  (let* ((horizontal (eq (urusi-layout--kind node) 'row))
         (shown (cl-remove-if-not #'urusi-layout--shown-p
                                  (urusi-layout--children node)))
         (definition (if horizontal 'ColumnDefinition 'RowDefinition))
         (length (if horizontal :Width :Height))
         (definitions nil)
         (cells nil)
         (position 0))
    (cl-loop for (child . more) on shown
             do (push `(,definition ,length ,(urusi-layout--length child)) definitions)
                (push (urusi-layout--in-cell (urusi-layout--node child frame)
                                             horizontal position)
                      cells)
                (cl-incf position)
             when more
             do (push `(,definition ,length ,(number-to-string urusi-layout-splitter-size))
                      definitions)
                (push (urusi-layout--splitter horizontal child (car more) position) cells)
                (cl-incf position))
    `(Grid (,(if horizontal 'Grid.ColumnDefinitions 'Grid.RowDefinitions)
            ,@(nreverse definitions))
           ,@(nreverse cells))))

(defun urusi-layout--node (node frame)
  "Return NODE, a part of the layout, as a tree, for FRAME."
  (if (eq (urusi-layout--kind node) 'panel)
      (urusi-layout--panel node frame)
    (urusi-layout--group node frame)))

(defun urusi-layout-component (frame)
  "Return `urusi-layout' as a tree, for `urusi-screen-components'.
It takes the room the other components leave."
  (if (urusi-layout--shown-p urusi-layout)
      (urusi-layout--node urusi-layout frame)
    '(Border)))

(put 'urusi-layout-component 'urusi-screen-fill t)

;;;; Changing it

(defun urusi-layout--set (id property value)
  "Set PROPERTY of the part whose :id is ID to VALUE, and show the change."
  (unless (urusi-layout--find id)
    (user-error "urusi-layout: No part is called %s" id))
  (puthash id (plist-put (gethash id urusi-layout--state) property value)
           urusi-layout--state)
  (urusi-screen--after-command))

(defun urusi-layout--read-id (prompt)
  "Ask for the :id of a part of the layout, with PROMPT."
  (let ((ids nil))
    (cl-labels ((collect (node)
                  (when-let* ((id (plist-get (urusi-layout--properties node) :id)))
                    (push (symbol-name id) ids))
                  (unless (eq (urusi-layout--kind node) 'panel)
                    (mapc #'collect (urusi-layout--children node)))))
      (collect urusi-layout))
    (intern (completing-read prompt (nreverse ids) nil t))))

(defun urusi-layout-show (id)
  "Show the part of the layout whose :id is ID."
  (interactive (list (urusi-layout--read-id "Show: ")))
  (urusi-layout--set id :hidden nil))

(defun urusi-layout-hide (id)
  "Hide the part of the layout whose :id is ID; the others take its room."
  (interactive (list (urusi-layout--read-id "Hide: ")))
  (urusi-layout--set id :hidden t))

(defun urusi-layout-toggle (id)
  "Hide the part of the layout whose :id is ID if it is shown, else show it."
  (interactive (list (urusi-layout--read-id "Toggle: ")))
  (urusi-layout--set id :hidden (not (urusi-layout--get (urusi-layout--find id)
                                                        :hidden))))

(defun urusi-layout-resize (id size)
  "Make the part of the layout whose :id is ID take SIZE pixels.
SIZE nil has it share what is left with the others instead."
  (interactive (list (urusi-layout--read-id "Resize: ")
                     (let ((text (read-string "Size (empty to share): ")))
                       (and (not (string-empty-p text)) (string-to-number text)))))
  (urusi-layout--set id :size size))

(defun urusi-layout-reset ()
  "Undo every change to the layout, back to `urusi-layout' as it is written."
  (interactive)
  (clrhash urusi-layout--state)
  (urusi-screen--after-command))

;;;; The splitters

(defun urusi-layout--dragged (event message)
  "Remember where a splitter was let go, as EVENT says in MESSAGE.
The host says how big the parts either side of it are now, and which
of them has a size of its own is the one that keeps it: a part that
shares what is left goes on sharing it.  If neither has one, the one
before gets one."
  (when (eq event 'splitter)
    (let* ((name (split-string (or (plist-get message :name) "") ":"))
           (before (and (nth 2 name) (urusi-layout--find (intern (nth 2 name)))))
           (after (and (nth 3 name) (urusi-layout--find (intern (nth 3 name))))))
      (cond
       ((and before (numberp (urusi-layout--get before :size)))
        (urusi-layout--set (intern (nth 2 name)) :size (plist-get message :before)))
       ((and after (numberp (urusi-layout--get after :size)))
        (urusi-layout--set (intern (nth 3 name)) :size (plist-get message :after)))
       (before
        (urusi-layout--set (intern (nth 2 name)) :size (plist-get message :before)))))))

(add-hook 'urusi-host-event-functions #'urusi-layout--dragged)

(provide 'urusi-layout)
;;; urusi-layout.el ends here
