#pragma once

#include "Window/FrameSizes.h"
#include "Input/Pointer.h"

#include <string>

namespace urusi::core::window
{
    // An Emacs frame, as what shows it can reach it.
    struct IFrameWindow
    {
        virtual ~IFrameWindow() = default;

        // Give the frame what the pointer did, as its mouse: where, in
        // XAML's units from the frame's corner, SCALE pixels of the
        // screen to one of them.
        virtual void PostPointer(input::PointerEvent const& event, double scale) = 0;

        // Tell the frame how big it is: Emacs lays its text out again.
        virtual void TellSize(PixelSize size) = 0;
    };

    // An Emacs frame shown in an element of the window: the frame the
    // window shows, or the frame of a panel. What the pointer does over
    // the element goes to the frame as the mouse, and the frame is as big
    // as the element.
    //
    // The frame is known by an id, the panel's, or the empty one for the
    // frame the window shows. How big each frame was last told is kept
    // in SIZES, which outlives the views, since a view is made again
    // whenever what Lisp built around it is.
    class FrameView
    {
    public:
        FrameView(std::wstring id, input::IPointerInputDevice& pointer, IFrameWindow& frame,
                  FrameSizes& sizes)
            : m_id(std::move(id)), m_pointer(pointer), m_frame(frame), m_sizes(sizes)
        {
        }

        std::wstring const& Id() const noexcept { return m_id; }

        // Take what the pointer did over the element, SCALE pixels of the
        // screen to one of XAML's. Return whether it was the frame's to
        // take: a move is, but is left to go on to whatever else wants
        // it.
        bool Pointer(input::PointerEvent const& event, double scale)
        {
            using input::PointerButton;
            using input::PointerKind;

            // A press or a release is of a button, or it is nothing the
            // mouse does; a pointer taken away is nothing to the frame.
            bool press = event.kind == PointerKind::Pressed && event.button != PointerButton::None;
            bool release = event.kind == PointerKind::Released && event.button != PointerButton::None;
            bool other = event.kind == PointerKind::Moved || event.kind == PointerKind::Wheel;
            if (!press && !release && !other)
            {
                return false;
            }

            // Taken while a button is down, so that a drag that leaves
            // the frame is followed to where it ends, and let go once
            // none is.
            if (press)
            {
                m_pointer.Capture();
            }
            m_frame.PostPointer(event, scale);
            if (release && !event.AnyButton())
            {
                m_pointer.Release();
            }
            return event.kind != PointerKind::Moved;
        }

        // The element is WIDTH by HEIGHT in XAML's units: tell the frame,
        // if that is not what it was told last.
        void Resized(double width, double height, double scale)
        {
            if (auto size = m_sizes.Resized(m_id, width, height, scale))
            {
                m_frame.TellSize(*size);
            }
        }

    private:
        std::wstring m_id;
        input::IPointerInputDevice& m_pointer;
        IFrameWindow& m_frame;
        FrameSizes& m_sizes;
    };
}
