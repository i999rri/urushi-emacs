#!/usr/bin/env bash
# What Emacs costs while nothing is happening, at one poll interval and
# at another:
#
#   scripts/verify/idle-cost.sh [seconds]
#
# The host has no way to wake Emacs, so urushi looks for its messages on
# a timer; each look brings Emacs out of idle and draws the screen
# again. This says what that comes to, by reading the process's own
# account of the time it spent.

set -euo pipefail

seconds=${1:-10}
emacs=$HOME/dev/urushi/emacs-build/src/emacs
config=$HOME/dev/urushi/emacs-config/.config/emacs
ticks=$(getconf CLK_TCK)

spent () {                      # utime + stime, in ticks
    awk '{print $14 + $15}' "/proc/$1/stat"
}

measure () {
    local label=$1 form=$2 pid before after

    EMACS_HOST_PIPE=1 EMACSLOADPATH=$HOME/dev/urushi/site-lisp: \
        "$emacs" --init-directory "$config" --eval "$form" \
        < /dev/null > /dev/null 2>&1 &
    pid=$!
    sleep 25
    before=$(spent "$pid")
    sleep "$seconds"
    after=$(spent "$pid")
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true

    awk -v a="$before" -v b="$after" -v t="$ticks" -v s="$seconds" -v l="$label" \
        'BEGIN { printf "%-28s %.2fs of CPU over %ss -- %.1f%%\n", l, (b - a) / t, s, (b - a) / t / s * 100 }'
}

measure "polling every 0.05s" "(ignore)"
measure "polling every 2s" "(progn (setq urushi-poll-interval 2) (urushi-start))"
