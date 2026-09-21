#pragma once

#include <cstdint>

namespace urusi::input
{
    // One key, pressed or let go, in the terms Windows would have put it
    // to a window of its own.
    struct KeyEvent
    {
        int key{ 0 };            // the virtual key; 0 for none
        uint32_t repeat{ 1 };
        uint32_t scanCode{ 0 };
        bool extended{ false };
        bool menuDown{ false };  // Alt is held
        bool wasDown{ false };   // it was already down: a repeat
        bool down{ true };       // pressed, or let go
    };

    // What the keyboard's side of the window can ask of the device the
    // keys come from: the element that holds the focus while typing, and
    // the input method's context there.
    struct IKeyInputDevice
    {
        virtual ~IKeyInputDevice() = default;

        // Tell the input method the keys come here, or have gone.
        virtual void NotifyFocusEnter() = 0;
        virtual void NotifyFocusLeave() = 0;
    };
}
