#!/usr/bin/env bash
# Puts urushi's own Lisp into the Emacs installed at EMACS, where Emacs
# keeps the Lisp that came with the installation, so that it is on the
# load path from the moment Emacs starts.  An init file can then
# require it and say what the screen should look like, which is the
# whole point of building the screen in Lisp.
#
#   scripts/install-urushi-lisp.sh EMACS
#
# EMACS is the directory with bin and share in it.

set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
emacs=${1:?which Emacs to install into}
site=$emacs/share/emacs/site-lisp

mkdir -p "$site/urushi"
for file in "$here"/lisp/*.el; do
    [ "$(basename "$file")" = urushi-site-start.el ] && continue
    cp -f "$file" "$site/urushi/"
done

# And the one file Emacs looks for by name before it reads the init
# file, which is where urushi is brought up around it.
cp -f "$here/lisp/urushi-site-start.el" "$site/site-start.el"
