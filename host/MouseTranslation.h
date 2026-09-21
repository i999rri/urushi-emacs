#pragma once

#include <windows.h>

#include <cmath>
#include <optional>

namespace urusi
{
    // A pointer as XAML reports it over the element a frame is shown in:
    // where it is on that element, in XAML's 96ths of an inch, and what
    // is held down.
    struct PointerState
    {
        double x{ 0 };
        double y{ 0 };
        double scale{ 1 };       // screen pixels to one of XAML's
        bool left{ false };
        bool right{ false };
        bool middle{ false };
        bool shift{ false };
        bool control{ false };
    };

    enum class PointerEvent { Pressed, Released, Moved, Wheel };

    // Which button a press or a release is of.
    enum class PointerButton { None, Left, Right, Middle };

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

    // What to post Emacs for EVENT, or nothing. The messages are those
    // Windows would have sent the frame's own window: where the pointer
    // is, counted from the corner of the frame in the pixels of the
    // screen, and which buttons and modifier keys are down, as the flags
    // of a mouse message carry them. Emacs makes of them what it makes
    // of any mouse, a click that moves the point, a drag that selects, a
    // wheel that scrolls.
    //
    // BUTTON is the button EVENT is a press or release of, and WHEEL how
    // far the wheel turned, HORIZONTAL if it is the sideways one.
    inline std::optional<MouseMessage> TranslatePointer(PointerEvent event,
                                                        PointerState const& pointer,
                                                        PointerButton button = PointerButton::None,
                                                        int wheel = 0,
                                                        bool horizontal = false) noexcept
    {
        int x = static_cast<int>(std::lround(pointer.x * pointer.scale));
        int y = static_cast<int>(std::lround(pointer.y * pointer.scale));

        WPARAM flags = 0;
        if (pointer.left) flags |= MK_LBUTTON;
        if (pointer.right) flags |= MK_RBUTTON;
        if (pointer.middle) flags |= MK_MBUTTON;
        if (pointer.shift) flags |= MK_SHIFT;
        if (pointer.control) flags |= MK_CONTROL;

        MouseMessage out;
        out.wParam = flags;
        out.lParam = MAKELPARAM(x, y);

        switch (event)
        {
        case PointerEvent::Pressed:
            switch (button)
            {
            case PointerButton::Left: out.message = WM_LBUTTONDOWN; break;
            case PointerButton::Right: out.message = WM_RBUTTONDOWN; break;
            case PointerButton::Middle: out.message = WM_MBUTTONDOWN; break;
            default: return std::nullopt;
            }
            out.capture = true;
            return out;

        case PointerEvent::Released:
            switch (button)
            {
            case PointerButton::Left: out.message = WM_LBUTTONUP; break;
            case PointerButton::Right: out.message = WM_RBUTTONUP; break;
            case PointerButton::Middle: out.message = WM_MBUTTONUP; break;
            default: return std::nullopt;
            }
            out.release = !(flags & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON));
            return out;

        case PointerEvent::Moved:
            out.message = WM_MOUSEMOVE;
            return out;

        case PointerEvent::Wheel:
            // A wheel message says where the pointer is on the screen
            // rather than on the window, and Emacs turns it into a place
            // on the frame from its own window. The frame's window is on
            // no screen and is at its corner, so the place on the frame
            // is what is given.
            out.message = horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
            out.wParam = MAKEWPARAM(static_cast<WORD>(flags),
                                    static_cast<WORD>(static_cast<short>(wheel)));
            return out;
        }
        return std::nullopt;
    }
}
