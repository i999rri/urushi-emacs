#pragma once

#include "Input/Pointer.h"

#include <windows.h>

#include <cmath>
#include <optional>

namespace urusi::windows::input
{
    // The message the frame's window is to be posted, and what to do
    // with the pointer: take it while a button is down, so that a drag
    // that leaves the frame is followed to where it ends, and let it go
    // once none is.
    struct MouseMessage
    {
        UINT message{ 0 };
        WPARAM wParam{ 0 };
        LPARAM lParam{ 0 };
        bool capture{ false };
        bool release{ false };
    };

    // What to post Emacs for EVENT, over the element a frame is shown
    // in, or nothing. The messages are those Windows would have sent the
    // frame's own window: where the pointer is, counted from the corner
    // of the frame in the pixels of the screen, SCALE of them to one of
    // XAML's, and which buttons and modifier keys are down, as the flags
    // of a mouse message carry them. Emacs makes of them what it makes
    // of any mouse, a click that moves the point, a drag that selects, a
    // wheel that scrolls.
    inline std::optional<MouseMessage> TranslatePointer(core::input::PointerEvent const& event,
                                                        double scale) noexcept
    {
        int x = static_cast<int>(std::lround(event.x * scale));
        int y = static_cast<int>(std::lround(event.y * scale));

        WPARAM flags = 0;
        if (event.left) flags |= MK_LBUTTON;
        if (event.right) flags |= MK_RBUTTON;
        if (event.middle) flags |= MK_MBUTTON;
        if (event.shift) flags |= MK_SHIFT;
        if (event.control) flags |= MK_CONTROL;

        MouseMessage out;
        out.wParam = flags;
        out.lParam = MAKELPARAM(x, y);

        switch (event.kind)
        {
        case core::input::PointerKind::Pressed:
            switch (event.button)
            {
            case core::input::PointerButton::Left: out.message = WM_LBUTTONDOWN; break;
            case core::input::PointerButton::Right: out.message = WM_RBUTTONDOWN; break;
            case core::input::PointerButton::Middle: out.message = WM_MBUTTONDOWN; break;
            default: return std::nullopt;
            }
            out.capture = true;
            return out;

        case core::input::PointerKind::Released:
            switch (event.button)
            {
            case core::input::PointerButton::Left: out.message = WM_LBUTTONUP; break;
            case core::input::PointerButton::Right: out.message = WM_RBUTTONUP; break;
            case core::input::PointerButton::Middle: out.message = WM_MBUTTONUP; break;
            default: return std::nullopt;
            }
            out.release = !event.AnyButton();
            return out;

        case core::input::PointerKind::Moved:
            out.message = WM_MOUSEMOVE;
            return out;

        case core::input::PointerKind::Wheel:
            // A wheel message says where the pointer is on the screen
            // rather than on the window, and Emacs turns it into a place
            // on the frame from its own window. The frame's window is on
            // no screen and is at its corner, so the place on the frame
            // is what is given.
            out.message = event.horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
            out.wParam = MAKEWPARAM(static_cast<WORD>(flags),
                                    static_cast<WORD>(static_cast<short>(event.wheel)));
            return out;

        case core::input::PointerKind::CaptureLost:
            return std::nullopt;
        }
        return std::nullopt;
    }
}
