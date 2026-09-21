#include <gtest/gtest.h>

#include "Window/FrameView.h"

#include <vector>

using urusi::core::window::FrameSizes;
using urusi::core::window::FrameView;
using urusi::core::window::PixelSize;
using urusi::core::input::PointerButton;
using urusi::core::input::PointerKind;

namespace
{
    struct Pointer : urusi::core::input::IPointerInputDevice
    {
        void Capture() override { captured = true; }
        void Release() override { captured = false; }
        bool captured{ false };
    };

    struct Frame : urusi::core::window::IFrameWindow
    {
        void PostPointer(urusi::core::input::PointerEvent const& event, double) override
        {
            posted.push_back(event.kind);
        }
        void TellSize(PixelSize size) override { sizes.push_back(size); }

        std::vector<PointerKind> posted;
        std::vector<PixelSize> sizes;
    };

    struct View
    {
        Pointer pointer;
        Frame frame;
        FrameSizes sizes;
        FrameView view{ L"", pointer, frame, sizes };
    };
}

// A drag: the pointer is taken while the button is down, so that the
// drag is followed past the edge of the frame, and let go after.
TEST(FrameViewTest, ADragIsFollowedToWhereItEnds)
{
    View view;
    EXPECT_TRUE(view.view.Pointer({ .kind = PointerKind::Pressed,
                                    .button = PointerButton::Left, .left = true }, 1));
    EXPECT_TRUE(view.pointer.captured);

    // A move is passed on, and left to whatever else wants it.
    EXPECT_FALSE(view.view.Pointer({ .kind = PointerKind::Moved, .x = -10, .left = true }, 1));
    EXPECT_TRUE(view.pointer.captured);

    EXPECT_TRUE(view.view.Pointer({ .kind = PointerKind::Released,
                                    .button = PointerButton::Left }, 1));
    EXPECT_FALSE(view.pointer.captured);
    EXPECT_EQ(view.frame.posted,
              (std::vector<PointerKind>{ PointerKind::Pressed, PointerKind::Moved,
                                         PointerKind::Released }));
}

TEST(FrameViewTest, WhatIsNotTheMousesIsNotPassedOn)
{
    View view;
    EXPECT_FALSE(view.view.Pointer({ .kind = PointerKind::Pressed }, 1));
    EXPECT_FALSE(view.view.Pointer({ .kind = PointerKind::CaptureLost }, 1));
    EXPECT_TRUE(view.frame.posted.empty());
}

// The frame is told its size when it changes, and a view made again,
// as views are with what Lisp builds around them, does not tell it again.
TEST(FrameViewTest, TheFrameIsToldItsSizeWhenItChanges)
{
    View view;
    view.view.Resized(800, 600, 1.5);
    view.view.Resized(800, 600, 1.5);
    EXPECT_EQ(view.frame.sizes, (std::vector<PixelSize>{ { 1200, 900 } }));

    FrameView again{ L"", view.pointer, view.frame, view.sizes };
    again.Resized(800, 600, 1.5);
    EXPECT_EQ(view.frame.sizes.size(), 1u);
}
