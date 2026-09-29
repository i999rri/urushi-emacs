#pragma once

#include "Input/KeyNames.h"

#include <optional>

namespace urushi::windows::input
{
    // What to send Emacs for KEY, as the keyboard layout of this thread
    // makes it, with the modifiers held now: for an Emacs that takes its
    // keys as messages, and so has no layout of its own to read.
    //
    // Called as the key arrives, while the state of the keyboard is
    // still the state it arrived in.
    std::optional<EmacsKey> ReadKey(core::input::KeyEvent const& key);
}
