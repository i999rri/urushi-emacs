#!/usr/bin/env bash
# Runs the tests of the host (tests/host) against the Emacs that
# scripts/linux/build-emacs.sh built:
#
#   nix-shell scripts/linux/shell.nix --run scripts/linux/test-host.sh

set -euo pipefail

here=$(cd "$(dirname "$0")/../.." && pwd)
dir=${URUSI_LINUX_DIR:-$HOME/dev/urusi}

export URUSI_TEST_EMACS=$dir/emacs-build/src/emacs
cd "$here"
python3 -m unittest discover -s tests/host "$@"
