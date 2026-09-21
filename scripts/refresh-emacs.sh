#!/usr/bin/env bash
# Rebuilds libemacs.dll after a change to Emacs's C, and puts it where
# the application loads it from, without building or installing the
# rest of Emacs again.  Run it in the MSYS2 mingw64 shell:
#
#   scripts/refresh-emacs.sh
#
# A change to a file or two under src/ needs a recompile of what
# changed, a link, a dump and a copy of two files: ten seconds or so on
# one core.  Building and installing all of Emacs again for it takes
# minutes and ties the machine up.  Anything wider -- a header most of
# src/ includes, the Lisp of Emacs itself, configure.ac -- wants
# scripts/build-emacs.sh and scripts/stage-emacs.sh, as does a first
# build.
#
# urusi's own Lisp goes along too, since where it runs from is some way
# from where it is edited.

set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
build=$here/external/emacs-build/src

# A change to one file compiles one file, so more at a time buys
# nothing; this is for the odd change to a header a few files share.
jobs=4

[ -f "$build/w32dll-smoke.exe" ] || {
    echo "no build in $build: scripts/build-emacs.sh first" >&2
    exit 1
}

echo "=== libemacs.dll"
make -C "$build" -j"$jobs" libemacs.dll

# The host of the dump has to be the DLL, so that the dump carries its
# fingerprint.  Dumping writes emacs.pdmp, which is emacs.exe's, so that
# one is kept aside while it happens.
echo "=== libemacs.pdmp"
(cd "$build"
 cp -f emacs.pdmp emacs.pdmp.exe
 ./w32dll-smoke.exe -batch -l loadup --temacs=pdump >/dev/null
 mv -f emacs.pdmp libemacs.pdmp
 mv -f emacs.pdmp.exe emacs.pdmp)

# The staged Emacs, and the copies the package's builds laid out,
# which is what running the application without building it again
# finds.
for emacs in "$here/emacs" \
             "$here"/platforms/windows/package/bin/*/*/AppX/emacs; do
    [ -d "$emacs/bin" ] || continue

    cp -f "$build/libemacs.dll" "$build/libemacs.pdmp" "$emacs/bin/"
    strip --strip-debug "$emacs/bin/libemacs.dll"
    "$here/scripts/install-urusi-lisp.sh" "$emacs"

    echo "refreshed ${emacs#"$here"/}"
done
