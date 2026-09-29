#!/usr/bin/env bash
# Builds Emacs from external/emacs into external/emacs-build, as
# libemacs.dll with a dump of its own, which is what this application
# loads.  Run it in the MSYS2 mingw64 shell:
#
#   scripts/build-emacs.sh
#
# Then scripts/stage-emacs.sh puts the result where the build finds it.
# Pass --deps to install the mingw64 packages Emacs is built against
# first, which is only needed once on a machine.
#
# Pass --host for the host window system, whose frames this application
# draws itself, into external/emacs-host-build:
#
#   scripts/build-emacs.sh --host
#   scripts/stage-emacs.sh external/emacs-host-build

set -euo pipefail

deps=
host=
while :; do
    case "${1-}" in
        --deps) deps=1; shift ;;
        # The host window system: frames with no window of their own,
        # which this application draws from what Emacs says to draw.
        # Built beside the w32 one rather than over it, so that both
        # are there to compare.
        --host) host=1; shift ;;
        *) break ;;
    esac
done

here=$(cd "$(dirname "$0")/.." && pwd)
src=$here/external/emacs
# Somewhere else with URUSHI_BUILD_DIR, to keep a build of another shape
# beside the one this application loads rather than over it.
build=${URUSHI_BUILD_DIR:-$here/external/emacs${host:+-host}-build}

[ -f "$src/configure.ac" ] || {
    echo "no Emacs in $src: git submodule update --init" >&2
    exit 1
}

if [ -n "$deps" ]; then
    echo "=== dependencies"
    pacman -S --noconfirm --needed \
        base-devel \
        git \
        mingw-w64-x86_64-toolchain \
        mingw-w64-x86_64-autotools \
        mingw-w64-x86_64-xpm-nox \
        mingw-w64-x86_64-gmp \
        mingw-w64-x86_64-gnutls \
        mingw-w64-x86_64-harfbuzz \
        mingw-w64-x86_64-libtree-sitter \
        mingw-w64-x86_64-sqlite3 \
        mingw-w64-x86_64-librsvg \
        mingw-w64-x86_64-libwebp \
        mingw-w64-x86_64-libxml2 \
        mingw-w64-x86_64-lcms2 \
        mingw-w64-x86_64-giflib \
        mingw-w64-x86_64-libjpeg-turbo \
        mingw-w64-x86_64-libpng \
        mingw-w64-x86_64-libtiff \
        mingw-w64-x86_64-zlib >/dev/null
fi

# Emacs does not build from a checkout with CRLF line endings:
# autoconf reads the stray return as part of a macro argument and
# stops at "'\' is already registered with AC_CONFIG_FILES".  Git on
# Windows converts on checkout unless told otherwise, and a submodule
# does not inherit that from the repository above it.
if grep -q $'\r' "$src/configure.ac"; then
    command -v git >/dev/null || {
        echo "$src was checked out with CRLF and git is not in this shell." >&2
        echo "Install it: pacman -S git, or run this with --deps." >&2
        exit 1
    }
    if [ -n "$(git -C "$src" status --porcelain)" ]; then
        echo "$src has changes of its own, and its files have to be laid" >&2
        echo "down again to lose the CRLF.  Deal with those first." >&2
        exit 1
    fi
    echo "=== line endings"
    git -C "$src" config core.autocrlf false
    git -C "$src" rm --cached -rq .
    git -C "$src" reset --hard -q
fi

# configure is not in the repository, and autogen.sh looks for git,
# which the mingw64 shell does not have; autoconf alone is what it
# needs here.
[ -f "$src/configure" ] || (cd "$src" && ./autogen.sh autoconf)

if [ ! -f "$build/Makefile" ]; then
    echo "=== configure"
    mkdir -p "$build"
    (cd "$build" && "$src/configure" ${host:+--with-host}         --without-dbus --with-native-compilation=no)
fi

# Four at a time.  Recompiling everything takes about three and a half
# minutes on one core and about one on four, and the rest of the
# machine stays with whoever is using it; every core buys the last few
# seconds of that and ties the machine up for them.  A change to one
# file gains nothing from more.
jobs=4

# emacs.exe first: the Lisp and the data it builds on the way are what
# the DLL is dumped with, and building it proves the fork still builds
# the ordinary way.
echo "=== emacs.exe"
make -C "$build" -j"$jobs"

echo "=== libemacs.dll"
make -C "$build/src" -j"$jobs" libemacs.dll

# The host of the dump has to be the DLL, so that the dump carries its
# fingerprint.  w32dll-smoke.exe is that host, and is worth keeping: it
# runs Emacs from the DLL without a window.
echo "=== w32dll-smoke.exe"
gcc -O2 -Wall -I"$src/libemacs/src" -o "$build/src/w32dll-smoke.exe" \
    "$src/libemacs/nt/w32dll-smoke.c" -Wl,--stack,0x00800000

echo "=== libemacs.pdmp"
cd "$build/src"
cp -f emacs.pdmp emacs.pdmp.exe
./w32dll-smoke.exe -batch -l loadup --temacs=pdump >/dev/null
mv -f emacs.pdmp libemacs.pdmp
mv -f emacs.pdmp.exe emacs.pdmp

./w32dll-smoke.exe --batch \
    --eval '(message "built %s, host bridge %S" emacs-version (fboundp (quote host-post)))'
