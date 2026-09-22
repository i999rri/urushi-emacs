#include "pch.h"
#include "Input/KeyboardLayout.h"

namespace
{
    // ToUnicodeEx is to leave the keyboard's state as it was: without
    // it, asking about a dead key would use it up, and the character it
    // was to go on would lose its accent.
    constexpr UINT kKeepKeyboardState = 0x4;

    bool Held(BYTE const* state, int key) noexcept
    {
        return (state[key] & 0x80) != 0;
    }

    // The character the layout types for KEY with the keys held in
    // STATE, or 0 if it types none, or only begins one, as a dead key
    // does.
    char32_t Character(urusi::core::input::KeyEvent const& key, BYTE const* state)
    {
        wchar_t text[4]{};
        int count = ToUnicodeEx(static_cast<UINT>(key.key), key.scanCode, state, text,
                                ARRAYSIZE(text), kKeepKeyboardState, GetKeyboardLayout(0));
        if (count == 1)
        {
            return text[0];
        }

        // A character past the first 65536 comes as two halves.
        bool pair = count == 2 && IS_HIGH_SURROGATE(text[0]) && IS_LOW_SURROGATE(text[1]);
        if (pair)
        {
            return 0x10000 + ((static_cast<char32_t>(text[0]) - 0xD800) << 10)
                + (static_cast<char32_t>(text[1]) - 0xDC00);
        }
        return 0;
    }
}

namespace urusi::windows::input
{
    std::optional<EmacsKey> ReadKey(core::input::KeyEvent const& key)
    {
        BYTE state[256]{};
        if (!GetKeyboardState(state))
        {
            return std::nullopt;
        }

        KeyModifiers held{
            .shift = Held(state, VK_SHIFT),
            .control = Held(state, VK_CONTROL),
            .alt = Held(state, VK_MENU),
            .windows = Held(state, VK_LWIN) || Held(state, VK_RWIN),
        };

        // AltGr is Ctrl and Alt to Windows, and where a layout types a
        // character with it, the character is the key: é, or @ on a
        // German keyboard, and not C-M-e or C-M-q.
        if (held.control && held.alt)
        {
            char32_t character = Character(key, state);
            if (character >= 0x20)
            {
                held.control = false;
                held.alt = false;
                return KeyForEmacs(key, character, held);
            }
        }

        // Ctrl and Alt are sent as modifiers, and the character is the
        // one the key types without them.
        for (int modifier : { VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU })
        {
            state[modifier] = 0;
        }
        return KeyForEmacs(key, Character(key, state), held);
    }
}
