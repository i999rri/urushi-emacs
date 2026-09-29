#!/usr/bin/env bash
# Builds the fork of Emacs for Linux, in WSL or on Linux itself, and
# leaves it where the tests of the host look for it:
#
#   nix-shell scripts/linux/shell.nix --run scripts/linux/build-emacs.sh
#
# The sources are a clone of the fork of its own, in the Linux file
# system (URUSHI_LINUX_DIR, ~/dev/urushi by default), rather than the
# tree Windows builds from: autogen.sh writes configure and the rest
# into the tree it runs in, which the two builds would then fight
# over, and building across to the Windows disk is slow. The clone is
# brought up to what the Windows tree has committed first.

set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
dir=${URUSHI_LINUX_DIR:-$HOME/dev/urushi}
src=$dir/emacs
build=$dir/emacs-build

if [ ! -d "$src" ]; then
    git clone --branch urushi "$here/external/emacs" "$src"
fi
git -C "$src" pull --ff-only "$here/external/emacs" urushi

[ -x "$src/configure" ] || (cd "$src" && ./autogen.sh)

mkdir -p "$build"
cd "$build"
# The host window system in place of X: its frames are the host's to
# draw, and it needs no library of the desktop's.
#
# Configured again when what is here was configured without it, as a
# tree from before --with-host existed was: that one looks built and
# builds an ordinary Emacs, which answers a pipe with "standard input is
# not a tty" and so fails every test of the host for a reason that says
# nothing about the host.
# Configured again by hand after shell.nix gains a library, too: configure
# writes down what it found, and a Makefile that is already here is not
# asked again.
if [ ! -f Makefile ] || ! grep -q '^#define HAVE_HOST' src/config.h; then
    "$src/configure" --with-host --without-x --with-gnutls=ifavailable \
        --without-native-compilation --without-pop --without-mailutils
fi

make -j"$(nproc)"
echo "built $build/src/emacs"
