#include <gtest/gtest.h>

#include "Input/PointerMessage.h"

using urusi::core::input::PointerButton;
using urusi::core::input::PointerEvent;
using urusi::core::input::PointerKind;
using urusi::windows::input::PointerForEmacs;

// Where the pointer is, in the pixels of the screen from the frame's
// corner, as the mouse messages of the frame's window say it.
TEST(PointerMessageTest, PlaceIsInPixelsOfTheScreen)
{
    auto pointer = PointerForEmacs({ .kind = PointerKind::Moved, .x = 10.4, .y = 20.6 }, 1.5);
    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->kind, "move");
    EXPECT_EQ(pointer->x, 16);
    EXPECT_EQ(pointer->y, 31);
    EXPECT_EQ(pointer->button, 0);
}

TEST(PointerMessageTest, ButtonsAreNumberedAsEmacsNumbersThem)
{
    std::pair<PointerButton, int> buttons[] = {
        { PointerButton::Left, 1 }, { PointerButton::Middle, 2 }, { PointerButton::Right, 3 },
    };
    for (auto [button, number] : buttons)
    {
        auto down = PointerForEmacs({ .kind = PointerKind::Pressed, .button = button }, 1.0);
        auto up = PointerForEmacs({ .kind = PointerKind::Released, .button = button }, 1.0);
        ASSERT_TRUE(down && up);
        EXPECT_EQ(down->kind, "down");
        EXPECT_EQ(up->kind, "up");
        EXPECT_EQ(down->button, number);
        EXPECT_EQ(up->button, number);
    }
}

TEST(PointerMessageTest, APressOfNoButtonIsNothing)
{
    EXPECT_FALSE(PointerForEmacs({ .kind = PointerKind::Pressed }, 1.0));
    EXPECT_FALSE(PointerForEmacs({ .kind = PointerKind::Released }, 1.0));
    EXPECT_FALSE(PointerForEmacs({ .kind = PointerKind::CaptureLost }, 1.0));
}

// A notch of the wheel is 120 and a line, away from the person and to
// the right positive, for Windows as for the protocol.
TEST(PointerMessageTest, WheelIsInLines)
{
    auto away = PointerForEmacs({ .kind = PointerKind::Wheel, .wheel = 120 }, 1.0);
    ASSERT_TRUE(away);
    EXPECT_EQ(away->kind, "wheel");
    EXPECT_DOUBLE_EQ(away->dy, 1.0);
    EXPECT_DOUBLE_EQ(away->dx, 0.0);

    auto toward = PointerForEmacs({ .kind = PointerKind::Wheel, .wheel = -360 }, 1.0);
    ASSERT_TRUE(toward);
    EXPECT_DOUBLE_EQ(toward->dy, -3.0);

    auto fraction = PointerForEmacs({ .kind = PointerKind::Wheel, .wheel = 30 }, 1.0);
    ASSERT_TRUE(fraction);
    EXPECT_DOUBLE_EQ(fraction->dy, 0.25);

    auto right = PointerForEmacs({ .kind = PointerKind::Wheel, .wheel = 240, .horizontal = true },
                                 1.0);
    ASSERT_TRUE(right);
    EXPECT_DOUBLE_EQ(right->dx, 2.0);
    EXPECT_DOUBLE_EQ(right->dy, 0.0);
}

TEST(PointerMessageTest, ModifiersAreSaid)
{
    auto pointer = PointerForEmacs({ .kind = PointerKind::Pressed, .button = PointerButton::Left,
                                     .left = true, .shift = true, .control = true },
                                   1.0);
    ASSERT_TRUE(pointer);
    EXPECT_EQ(pointer->modifiers, (std::vector<std::string>{ "ctrl", "shift" }));
}
