#pragma once

#include "Input/KeyInput.h"
#include "Input/KeyTranslation.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace urusi::windows::input
{
    // Which modifier keys are held, as Windows has them.
    struct KeyModifiers
    {
        bool shift{ false };
        bool control{ false };
        bool alt{ false };
        bool windows{ false };
    };

    // A key as the protocol's `key` says it: the name Emacs has for it,
    // or the character it types, and the modifiers held.
    struct EmacsKey
    {
        bool down{ true };
        bool repeat{ false };
        std::string name;           // empty when it is a character
        char32_t character{ 0 };    // 0 when it has a name
        std::vector<std::string> modifiers;
    };

    // The name Emacs gives the virtual key KEY, or null if it has none
    // and is a character or nothing. EXTENDED tells the keypad's Enter
    // from the other one, which Windows gives the same virtual key.
    //
    // The names are those of keyboard.c's lispy_function_keys for X,
    // which is the table the host window system looks names up in: a
    // name that is not in it is dropped there. Return, Tab, Backspace
    // and Escape are named, although the layout gives them characters,
    // because Emacs binds them by name.
    inline char const* KeyName(int key, bool extended) noexcept
    {
        static char const* const kFunctionKeys[] = {
            "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
            "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23", "f24",
        };
        static char const* const kKeypadDigits[] = {
            "kp-0", "kp-1", "kp-2", "kp-3", "kp-4", "kp-5", "kp-6", "kp-7", "kp-8", "kp-9",
        };

        if (key >= VK_F1 && key <= VK_F24)
        {
            return kFunctionKeys[key - VK_F1];
        }
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        {
            return kKeypadDigits[key - VK_NUMPAD0];
        }

        switch (key)
        {
        case VK_RETURN: return extended ? "kp-enter" : "return";
        case VK_TAB: return "tab";
        case VK_BACK: return "backspace";
        case VK_ESCAPE: return "escape";
        case VK_DELETE: return "delete";
        case VK_INSERT: return "insert";
        case VK_HOME: return "home";
        case VK_END: return "end";
        case VK_PRIOR: return "prior";
        case VK_NEXT: return "next";
        case VK_LEFT: return "left";
        case VK_RIGHT: return "right";
        case VK_UP: return "up";
        case VK_DOWN: return "down";
        case VK_CLEAR: return "clear";
        case VK_PAUSE: return "pause";
        case VK_CANCEL: return "cancel";
        case VK_SNAPSHOT: return "print";
        case VK_APPS: return "menu";
        case VK_HELP: return "help";
        case VK_SELECT: return "select";
        case VK_EXECUTE: return "execute";
        case VK_ADD: return "kp-add";
        case VK_SUBTRACT: return "kp-subtract";
        case VK_MULTIPLY: return "kp-multiply";
        case VK_DIVIDE: return "kp-divide";
        case VK_DECIMAL: return "kp-decimal";
        case VK_SEPARATOR: return "kp-separator";
        default: return nullptr;
        }
    }

    // Whether KEY only changes what the other keys mean. Emacs sees
    // the modifiers on the keys they are held with, and has no event
    // for one pressed alone.
    inline bool IsModifierKey(int key) noexcept
    {
        switch (key)
        {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
        default:
            return false;
        }
    }

    // The protocol's names of the modifiers HELD. Alt is meta and the
    // Windows key super, as they are to Emacs on Windows by default.
    inline std::vector<std::string> ModifierNames(KeyModifiers held)
    {
        std::vector<std::string> names;
        if (held.control) names.emplace_back("ctrl");
        if (held.alt) names.emplace_back("meta");
        if (held.shift) names.emplace_back("shift");
        if (held.windows) names.emplace_back("super");
        return names;
    }

    // What to send Emacs for KEY, or nothing. CHARACTER is what the
    // keyboard layout types for it with the modifiers held other than
    // Ctrl and Alt, or 0 if it types nothing: Ctrl and Alt are sent as
    // modifiers, so that C-a comes as the character a with ctrl, the
    // way Emacs reads it from any other window system.
    //
    // The keys the input method works with are not sent, nor what it
    // is making something of: what it makes of them comes as text. A
    // modifier alone is not sent either, and neither is a key with no
    // name that types no character, a dead key for one.
    inline std::optional<EmacsKey> KeyForEmacs(core::input::KeyEvent const& key,
                                               char32_t character, KeyModifiers held)
    {
        if (key.key == 0 || IsInputMethodKey(key.key) || IsModifierKey(key.key))
        {
            return std::nullopt;
        }

        EmacsKey out;
        out.down = key.down;
        out.repeat = key.down && key.wasDown;
        out.modifiers = ModifierNames(held);

        if (auto name = KeyName(key.key, key.extended))
        {
            out.name = name;
            return out;
        }

        // What is left of the control characters is nothing to type.
        bool printable = character >= 0x20 && character != 0x7F;
        if (!printable)
        {
            return std::nullopt;
        }
        out.character = character;
        return out;
    }
}
