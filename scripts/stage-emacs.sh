#!/usr/bin/env bash
# Puts Emacs under emacs/ in this project, in the layout it expects:
# the DLL in bin, its dump beside it, its Lisp and data under share.
# The application loads it from there by a path of its own, so nothing
# has to be told where Emacs is.
#
# Run it in the MSYS2 mingw64 shell, with the directory Emacs was built
# in:
#
#   scripts/stage-emacs.sh ~/source/repos/emacs-build
#
# Pass --debug to keep the debug information in the DLL, which makes it
# 145 MB instead of 4 MB.

set -euo pipefail

debug=
if [ "${1-}" = --debug ]; then
    debug=1
    shift
fi

build=${1:?usage: stage-emacs.sh [--debug] <emacs build directory>}
stage=$(cd "$(dirname "$0")/.." && pwd)/emacs

[ -f "$build/src/libemacs.dll" ] || {
    echo "$build/src/libemacs.dll is missing: make -C src libemacs.dll" >&2
    exit 1
}
[ -f "$build/src/libemacs.pdmp" ] || {
    echo "$build/src/libemacs.pdmp is missing: dump with the DLL" >&2
    exit 1
}

echo "staging into $stage"
rm -rf "$stage"
make -C "$build" install prefix="$stage" >/dev/null

# The installed programs are Emacs as a program, which is not how this
# runs it.  The dump under libexec belongs to emacs.exe.
rm -rf "$stage/bin" "$stage/include" "$stage/lib"
rm -f "$stage"/libexec/emacs/*/*/*.pdmp
rm -rf "$stage/share/applications" "$stage/share/icons" \
   "$stage/share/info" "$stage/share/man" "$stage/share/metainfo"

mkdir -p "$stage/bin"
cp "$build/src/libemacs.dll" "$stage/bin/"
cp "$build/src/libemacs.pdmp" "$stage/bin/"
[ -n "$debug" ] || strip --strip-debug "$stage/bin/libemacs.dll"

# Whatever the DLL needs from mingw64, and whatever those need in turn.
mingw_deps () { ldd "$1" 2>/dev/null | awk '/mingw64/ { print $3 }'; }
pending=$(mingw_deps "$stage/bin/libemacs.dll")
while [ -n "$pending" ]; do
    next=
    for dependency in $pending; do
        name=$(basename "$dependency")
        [ -f "$stage/bin/$name" ] && continue
        cp "$dependency" "$stage/bin/"
        next="$next $(mingw_deps "$dependency")"
    done
    pending=$next
done

echo "staged $(du -sh "$stage" | cut -f1) in $(find "$stage" -type f | wc -l) files"
ls "$stage/bin"
