#pragma once

#include "Input/Pointer.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace urushi::windows::input
{
    // What the pointer did, as the protocol's `pointer` says it.
    struct EmacsPointer
    {
        std::string kind;       // down, up, move or wheel
        int button{ 0 };        // for down and up: 1 left, 2 middle, 3 right
        int x{ 0 };
        int y{ 0 };
        double dx{ 0 };         // for wheel, in lines
        double dy{ 0 };
        std::vector<std::string> modifiers;
    };

    // What to send Emacs for EVENT, over the element a frame is shown
    // in, or nothing. Where it is is counted from the corner of the
    // frame in the pixels of the screen, SCALE of them to one of XAML's,
    // as the mouse messages of the frame's window count it.
    //
    // A turn of the wheel is WHEEL_DELTA, 120, to a line, and is
    // positive away from the person and to the right, for Windows as
    // for the protocol.
    inline std::optional<EmacsPointer> PointerForEmacs(core::input::PointerEvent const& event,
                                                       double scale)
    {
        constexpr double kWheelDelta = 120.0;

        EmacsPointer out;
        out.x = static_cast<int>(std::lround(event.x * scale));
        out.y = static_cast<int>(std::lround(event.y * scale));
        if (event.control) out.modifiers.emplace_back("ctrl");
        if (event.shift) out.modifiers.emplace_back("shift");

        switch (event.button)
        {
        case core::input::PointerButton::Left: out.button = 1; break;
        case core::input::PointerButton::Middle: out.button = 2; break;
        case core::input::PointerButton::Right: out.button = 3; break;
        case core::input::PointerButton::None: break;
        }

        bool buttonKind = event.kind == core::input::PointerKind::Pressed
            || event.kind == core::input::PointerKind::Released;
        if (buttonKind && out.button == 0)
        {
            return std::nullopt;
        }

        switch (event.kind)
        {
        case core::input::PointerKind::Pressed:
            out.kind = "down";
            return out;
        case core::input::PointerKind::Released:
            out.kind = "up";
            return out;
        case core::input::PointerKind::Moved:
            out.kind = "move";
            out.button = 0;
            return out;
        case core::input::PointerKind::Wheel:
            out.kind = "wheel";
            out.button = 0;
            (event.horizontal ? out.dx : out.dy) = event.wheel / kWheelDelta;
            return out;
        case core::input::PointerKind::CaptureLost:
            return std::nullopt;
        }
        return std::nullopt;
    }
}
