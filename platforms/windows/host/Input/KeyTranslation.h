#pragma once

#include "Input/KeyInput.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace urusi::windows::input
{
    // The message Emacs's frame is to be posted for a key.
    struct KeyMessage
    {
        UINT message{ 0 };
        WPARAM wParam{ 0 };
        LPARAM lParam{ 0 };
    };

    // Whether KEY is one the input method answers itself, rather than
    // one that means a character.
    //
    // These are the keys that turn it on and off and work its way
    // through a conversion, and the one Windows sends in place of a key
    // it has already handled. None of them is Emacs's to see, and
    // taking them here is what stops the input method being switched at
    // all.
    inline bool IsInputMethodKey(int key) noexcept
    {
        switch (key)
        {
        case VK_KANA:           // and VK_HANGUL: the same number
        case VK_JUNJA:
        case VK_FINAL:
        case VK_KANJI:          // and VK_HANJA
        case VK_CONVERT:
        case VK_NONCONVERT:
        case VK_ACCEPT:
        case VK_MODECHANGE:
        case VK_PROCESSKEY:
        case VK_OEM_ATTN:
        case VK_OEM_AUTO:
        case VK_OEM_ENLW:
        case VK_OEM_BACKTAB:
            return true;
        default:
            return false;
        }
    }

    // Whether KEY turns the input method on or off, rather than working
    // its way through a conversion.
    //
    // It is the one key of those above the window has to know of: the
    // input method leaves the composition it was holding where it is
    // and says so to nobody, so nothing else would end it.
    inline bool IsInputMethodSwitch(int key) noexcept
    {
        switch (key)
        {
        case VK_KANJI:          // the hankaku/zenkaku key, which Alt+` is sent as
        case VK_IME_ON:
        case VK_IME_OFF:
        case VK_OEM_AUTO:       // hankaku and zenkaku where each is a key of its own
        case VK_OEM_ENLW:
            return true;
        default:
            return false;
        }
    }

    // What to post Emacs for KEY, or nothing.
    //
    // A key the input method is making something of is not a key: what
    // it settles on arrives as text, and passing the key on as well
    // would type it twice. A key that works the input method itself is
    // not a key either, and is left alone entirely.
    //
    // Otherwise it is the message Windows would have sent: its lParam
    // carries the repeat count, the scan code, whether it is an extended
    // key, whether Alt is held, whether it was down, and whether it is
    // being released, where Emacs reads them.
    inline std::optional<KeyMessage> TranslateKey(core::input::KeyEvent const& key) noexcept
    {
        if (key.key == 0 || IsInputMethodKey(key.key))
        {
            return std::nullopt;
        }

        KeyMessage out;
        out.wParam = static_cast<WPARAM>(key.key);
        out.lParam = static_cast<LPARAM>(key.repeat & 0xFFFF)
            | (static_cast<LPARAM>(key.scanCode & 0xFF) << 16)
            | (key.extended ? (LPARAM{ 1 } << 24) : 0)
            | (key.menuDown ? (LPARAM{ 1 } << 29) : 0)
            | (key.wasDown ? (LPARAM{ 1 } << 30) : 0)
            | (key.down ? 0 : (LPARAM{ 1 } << 31));

        // Alt held is a system key to Windows and the meta key to
        // Emacs, and it arrives under another name.
        out.message = key.menuDown
            ? (key.down ? WM_SYSKEYDOWN : WM_SYSKEYUP)
            : (key.down ? WM_KEYDOWN : WM_KEYUP);
        return out;
    }
}
