#!/usr/bin/env bash
# How long each part of rebuilding Emacs takes on one core, which is
# what decides whether building on every core is worth the machine it
# ties up.  Run it in the MSYS2 mingw64 shell:
#
#   scripts/verify/time-emacs-build.sh           the usual change: one file
#   scripts/verify/time-emacs-build.sh --full    every object, as after a header
#
# It touches sources to force the work, so the next build does less.

set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
src=$here/external/emacs/src
build=$here/external/emacs-build/src

now () { date +%s%N; }
seconds () { awk -v ns="$1" 'BEGIN { printf "%.1f", ns / 1e9 }'; }

cd "$build"

if [ "${1-}" = --full ]; then
    # Every object that is built from src/, one at a time.
    objects=$(ls ./*.o | xargs -n1 basename)
    start=$(now)
    # Only sources that are there: some objects are built from a source
    # of another name, and touching one that is not there makes it.
    for object in $objects; do
        if [ -f "$src/${object%.o}.c" ]; then touch "$src/${object%.o}.c"; fi
    done
    make -j1 $objects >/dev/null 2>&1
    echo "compile $(echo "$objects" | wc -w) objects: $(seconds $(( $(now) - start )))s"
else
    touch "$src/hostscreen.c"
    start=$(now)
    make -j1 hostscreen.o >/dev/null 2>&1
    echo "compile one file:  $(seconds $(( $(now) - start )))s"
fi

start=$(now)
make -j1 libemacs.dll >/dev/null 2>&1
echo "link libemacs.dll: $(seconds $(( $(now) - start )))s"

# The dump is Emacs loading all of its Lisp and writing itself out,
# which is one process whatever -j says.
cp -f emacs.pdmp emacs.pdmp.exe
start=$(now)
./w32dll-smoke.exe -batch -l loadup --temacs=pdump >/dev/null 2>&1
echo "dump libemacs:     $(seconds $(( $(now) - start )))s"
mv -f emacs.pdmp libemacs.pdmp
mv -f emacs.pdmp.exe emacs.pdmp
