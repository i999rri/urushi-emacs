#!/usr/bin/env bash
# Puts urusi's own Lisp into the Emacs installed at EMACS, where Emacs
# keeps the Lisp that came with the installation, so that it is on the
# load path from the moment Emacs starts.  An init file can then
# require it and say what the screen should look like, which is the
# whole point of building the screen in Lisp.
#
#   scripts/install-urusi-lisp.sh EMACS
#
# EMACS is the directory with bin and share in it.

set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
emacs=${1:?which Emacs to install into}
site=$emacs/share/emacs/site-lisp

mkdir -p "$site/urusi"
for file in "$here"/lisp/*.el; do
    [ "$(basename "$file")" = urusi-site-start.el ] && continue
    cp -f "$file" "$site/urusi/"
done

# And the one file Emacs looks for by name before it reads the init
# file, which is where urusi is brought up around it.
cp -f "$here/lisp/urusi-site-start.el" "$site/site-start.el"
