#pragma once

namespace urusi
{
    enum class PointerKind { Pressed, Moved, Released, Wheel, CaptureLost };

    // Which button a press or a release is of.
    enum class PointerButton { None, Left, Right, Middle };

    // One thing a pointer did, over whatever it was over, in terms that
    // are nobody's toolkit's: where it is, in XAML's 96ths of an inch
    // from the corner of what it is over, and what is held down.
    struct PointerEvent
    {
        PointerKind kind{ PointerKind::Moved };
        double x{ 0 };
        double y{ 0 };
        PointerButton button{ PointerButton::None };
        bool left{ false };
        bool right{ false };
        bool middle{ false };
        bool shift{ false };
        bool control{ false };
        int wheel{ 0 };             // how far the wheel turned
        bool horizontal{ false };   // the sideways wheel

        bool AnyButton() const noexcept { return left || right || middle; }
    };

    // What something the pointer is over can ask of it.
    struct IPointerDevice
    {
        virtual ~IPointerDevice() = default;

        // Take the pointer, so that what it does is told here even
        // once it has left, as a drag that goes past the edge; or let
        // it go.
        virtual void Capture() = 0;
        virtual void Release() = 0;
    };
}
