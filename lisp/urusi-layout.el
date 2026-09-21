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
;;   frame       an Emacs frame of the panel's own, showing the :buffer
;;               it names at first, or *scratch*
;;   nil         nothing, a space for the sake of a space
;;   a function  called with the frame, returning a tree for `urusi-render'
;;   a tree      put there as it is
;;
;; and :background, :padding, :margin and :corner-radius, if it has
;; them, are the panel's own.
;;
;; A panel with a frame of its own is where a buffer can be sent, with
;; `urusi-layout-display-in-panel' in `display-buffer-alist':
;;
;;   (add-to-list 'display-buffer-alist
;;                '("\\*compilation\\*"
;;                  (urusi-layout-display-in-panel)
;;                  (panel . output)))
;;
;; Clicking in it selects it, and what is typed goes to it, as it does to
;; any window that is selected.

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

(defvar urusi-layout--frames (make-hash-table :test #'eq)
  "The frame of each panel that has one of its own, by the panel's :id.")

(defvar urusi-layout-make-frame-functions nil
  "Functions to run when the frame of a panel has been made.
Each is called with the frame and the :id of its panel, and can make it
what that panel needs: an output that has no use for tabs above it, for
one, can have its window show none.")

(defun urusi-layout-frame (id)
  "Return the frame of the panel whose :id is ID, making it if there is none.
It is a child of the root frame, so that it is drawn by the host as the
root frame is and has its keys from the same place, and it is drawn in
its panel rather than over the root frame.  It has no minibuffer of its
own: the root frame's is the one it uses.

Making it does not select it: a panel that comes into being, to show
the output of a compilation for one, is not where typing is to go."
  (let ((frame (gethash id urusi-layout--frames)))
    (unless (frame-live-p frame)
      (let* ((selected (selected-frame))
             (panel (urusi-layout--find id))
             (name (or (plist-get (urusi-layout--properties panel) :buffer)
                       "*scratch*"))
             (buffer (or (get-buffer name)
                         ;; One made here is a place for what is to come,
                         ;; the output of a compilation for one, and has
                         ;; nothing to type in yet: q quits it.
                         (with-current-buffer (get-buffer-create name)
                           (special-mode)
                           (current-buffer)))))
        (setq frame (make-frame `((parent-frame . ,(urusi-root-frame))
                                  (urusi-panel . ,id)
                                  (name . ,(format "urusi-%s" id))
                                  (minibuffer . nil)
                                  (undecorated . t)
                                  (left . 0) (top . 0)
                                  (width . 80) (height . 10)
                                  (internal-border-width . 0)
                                  (child-frame-border-width . 0)
                                  (tab-bar-lines . 0)
                                  (vertical-scroll-bars . nil)
                                  (no-other-frame . t)
                                  (no-focus-on-map . t))))
        (set-window-buffer (frame-root-window frame) buffer)
        (puthash id frame urusi-layout--frames)
        (run-hook-with-args 'urusi-layout-make-frame-functions frame id)
        (when (frame-live-p selected)
          (select-frame selected 'norecord))))
    frame))

(defun urusi-layout--content (content frame &optional id)
  "Return CONTENT, what a panel holds, as a tree, for FRAME.
ID is the :id of the panel, which a frame of its own is known by."
  (cond
   ((null content) '(Border))
   ((eq content 'emacs) (urusi-screen-frame-site frame))
   ((eq content 'frame)
    (unless id
      (error "urusi-layout: A panel with a frame of its own needs an :id"))
    (urusi-screen-frame-site (urusi-layout-frame id) (symbol-name id)))
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
             ,(urusi-layout--content (plist-get properties :content) frame
                                     (plist-get properties :id)))))

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

(defun urusi-layout--frame-in (node)
  "Return non-nil if the selected frame is the frame of a panel in NODE."
  (let ((selected (frame-parameter (selected-frame) 'urusi-panel)))
    (and selected
         (or (eq (plist-get (urusi-layout--properties node) :id) selected)
             (and (not (eq (urusi-layout--kind node) 'panel))
                  (cl-some #'urusi-layout--frame-in (urusi-layout--children node)))))))

(defun urusi-layout--set (id property value)
  "Set PROPERTY of the part whose :id is ID to VALUE, and show the change.
A part hidden while the frame of a panel in it is selected leaves the
root frame selected: what is typed next is not to go somewhere that
cannot be seen."
  (let ((node (urusi-layout--find id)))
    (unless node
      (user-error "urusi-layout: No part is called %s" id))
    (when (and (eq property :hidden) value (urusi-layout--frame-in node))
      (select-frame (urusi-root-frame) 'norecord)
      (redirect-frame-focus (urusi-root-frame) nil)))
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

;;;; Sending buffers to panels

(defun urusi-layout-display-in-panel (buffer alist)
  "Show BUFFER in the frame of a panel, for `display-buffer'.
The panel is the one whose :id is the `panel' entry of ALIST.  It is
shown if it is hidden, and BUFFER goes in the window of its frame that
is selected."
  (when-let* ((id (alist-get 'panel alist))
              ((urusi-layout--find id)))
    (let ((window (frame-selected-window (urusi-layout-frame id))))
      (when (urusi-layout--get (urusi-layout--find id) :hidden)
        (urusi-layout-show id))
      (window--display-buffer buffer window 'reuse alist))))

;;;; Quitting a panel

(defcustom urusi-layout-quit-hides-panel t
  "Whether quitting a window in the frame of a panel hides the panel.
`quit-window', which q is in most buffers that show something rather
than hold something to edit, would otherwise show another buffer in its
place, when what was wanted was the panel out of the way."
  :type 'boolean)

(defun urusi-layout--quit-window (quit &optional kill window)
  "Hide the panel WINDOW is in, if it is in one; else QUIT it, KILL and all.
The buffer is left as it is, to be there when the panel is shown again,
unless KILL says it is to go."
  (let* ((window (window-normalize-window window))
         (id (frame-parameter (window-frame window) 'urusi-panel)))
    (if (not (and urusi-layout-quit-hides-panel id (urusi-layout--find id)))
        (funcall quit kill window)
      (when kill
        (kill-buffer (window-buffer window)))
      (urusi-layout-hide id))))

(advice-add 'quit-window :around #'urusi-layout--quit-window)

;;;; Following the frame that is selected

(defun urusi-layout--follow-selection ()
  "Send what is typed to the frame that is selected, if it is a panel's.
The keys arrive at the root frame, and go where its focus is sent.  A
panel's frame selected by a click, or by any other means, is where they
are to go from then on; the root frame selected again takes them back.
A floating minibuffer sends them to itself, and is left to."
  (let* ((selected (selected-frame))
         (root (urusi-root-frame selected))
         (focus (frame-focus root)))
    (cond
     ((and (frame-parameter selected 'urusi-panel)
           (not (eq focus selected)))
      (redirect-frame-focus root selected))
     ((and (eq selected root)
           (frame-live-p focus)
           (frame-parameter focus 'urusi-panel))
      (redirect-frame-focus root nil)))))

(add-hook 'post-command-hook #'urusi-layout--follow-selection)

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
