#include <gtest/gtest.h>

#include "Input/MouseTranslation.h"

#include <windowsx.h>

using urusi::input::PointerButton;
using urusi::input::PointerEvent;
using urusi::input::PointerKind;
using urusi::input::TranslatePointer;

// Where the pointer is, in the pixels of the screen: XAML counts in
// 96ths of an inch, and at 150% there are one and a half of those.
TEST(MouseTranslationTest, WhereIsInThePixelsOfTheScreen)
{
    auto message = TranslatePointer({ .kind = PointerKind::Moved, .x = 10, .y = 20.5 }, 1.5);
    ASSERT_TRUE(message);
    EXPECT_EQ(message->message, static_cast<UINT>(WM_MOUSEMOVE));
    EXPECT_EQ(GET_X_LPARAM(message->lParam), 15);
    EXPECT_EQ(GET_Y_LPARAM(message->lParam), 31);
}

// Dragged past the corner of the frame, the place is less than nothing,
// as Windows would say it.
TEST(MouseTranslationTest, OutsideTheFrameIsLessThanNothing)
{
    auto message = TranslatePointer({ .kind = PointerKind::Moved, .x = -4, .y = -2,
                                      .left = true }, 1);
    ASSERT_TRUE(message);
    EXPECT_EQ(GET_X_LPARAM(message->lParam), -4);
    EXPECT_EQ(GET_Y_LPARAM(message->lParam), -2);
    EXPECT_EQ(message->wParam, static_cast<WPARAM>(MK_LBUTTON));
}

TEST(MouseTranslationTest, APressTakesThePointer)
{
    auto message = TranslatePointer({ .kind = PointerKind::Pressed,
                                      .button = PointerButton::Left,
                                      .left = true, .shift = true }, 1);
    ASSERT_TRUE(message);
    EXPECT_EQ(message->message, static_cast<UINT>(WM_LBUTTONDOWN));
    EXPECT_EQ(message->wParam, static_cast<WPARAM>(MK_LBUTTON | MK_SHIFT));
    EXPECT_TRUE(message->capture);
}

// Let go only once no button is down: a right button let go while the
// left is still held goes on following the drag.
TEST(MouseTranslationTest, ThePointerIsLetGoOnceNoButtonIsDown)
{
    auto right = TranslatePointer({ .kind = PointerKind::Released,
                                    .button = PointerButton::Right, .left = true }, 1);
    ASSERT_TRUE(right);
    EXPECT_EQ(right->message, static_cast<UINT>(WM_RBUTTONUP));
    EXPECT_FALSE(right->release);

    auto left = TranslatePointer({ .kind = PointerKind::Released,
                                   .button = PointerButton::Left }, 1);
    ASSERT_TRUE(left);
    EXPECT_EQ(left->message, static_cast<UINT>(WM_LBUTTONUP));
    EXPECT_TRUE(left->release);
}

TEST(MouseTranslationTest, APressOfNoButtonIsNothing)
{
    EXPECT_FALSE(TranslatePointer({ .kind = PointerKind::Pressed }, 1));
    EXPECT_FALSE(TranslatePointer({ .kind = PointerKind::CaptureLost }, 1));
}

TEST(MouseTranslationTest, TheWheelSaysHowFarAndWhichWay)
{
    auto down = TranslatePointer({ .kind = PointerKind::Wheel, .control = true,
                                   .wheel = -120 }, 1);
    ASSERT_TRUE(down);
    EXPECT_EQ(down->message, static_cast<UINT>(WM_MOUSEWHEEL));
    EXPECT_EQ(GET_WHEEL_DELTA_WPARAM(down->wParam), -120);
    EXPECT_EQ(GET_KEYSTATE_WPARAM(down->wParam), MK_CONTROL);

    auto sideways = TranslatePointer({ .kind = PointerKind::Wheel, .wheel = 120,
                                       .horizontal = true }, 1);
    ASSERT_TRUE(sideways);
    EXPECT_EQ(sideways->message, static_cast<UINT>(WM_MOUSEHWHEEL));
}
