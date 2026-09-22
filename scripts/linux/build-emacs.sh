#!/usr/bin/env bash
# Builds the fork of Emacs for Linux, in WSL or on Linux itself, and
# leaves it where the tests of the host look for it:
#
#   nix-shell scripts/linux/shell.nix --run scripts/linux/build-emacs.sh
#
# The sources are a clone of the fork of its own, in the Linux file
# system (URUSI_LINUX_DIR, ~/dev/urusi by default), rather than the
# tree Windows builds from: autogen.sh writes configure and the rest
# into the tree it runs in, which the two builds would then fight
# over, and building across to the Windows disk is slow. The clone is
# brought up to what the Windows tree has committed first.

set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
dir=${URUSI_LINUX_DIR:-$HOME/dev/urusi}
src=$dir/emacs
build=$dir/emacs-build

if [ ! -d "$src" ]; then
    git clone --branch urusi "$here/external/emacs" "$src"
fi
git -C "$src" pull --ff-only "$here/external/emacs" urusi

[ -x "$src/configure" ] || (cd "$src" && ./autogen.sh)

mkdir -p "$build"
cd "$build"
[ -f Makefile ] || "$src/configure" --without-x --with-gnutls=ifavailable \
    --without-native-compilation --without-pop --without-mailutils

make -j"$(nproc)"
echo "built $build/src/emacs"
