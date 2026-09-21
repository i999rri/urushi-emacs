#include <gtest/gtest.h>

#include "Window/CaptionRegions.h"

#include <vector>

using urusi::core::window::Area;
using urusi::core::window::CaptionRegions;
using urusi::core::window::PixelRect;

namespace
{
    // Windows, as far as its non-client regions go, counting what it is
    // told.
    struct Windows : urusi::core::window::INonClientRegions
    {
        void Set(PixelRect c, std::vector<PixelRect> const& p) override
        {
            caption = c;
            passthrough = p;
            ++told;
        }
        void Clear() override
        {
            caption.reset();
            passthrough.clear();
            ++told;
        }

        std::optional<PixelRect> caption;
        std::vector<PixelRect> passthrough;
        int told{ 0 };
    };
}

// The title bar Lisp drew moves the window, and its buttons are clicked,
// in the pixels of the screen.
TEST(CaptionRegionsTest, TheTitleBarIsTheCaptionAndItsControlsAreClicked)
{
    Windows windows;
    CaptionRegions regions{ windows };
    regions.Update(Area{ 0, 0, 800, 32 }, { Area{ 662, 0, 46, 32 }, Area{ 754, 0, 46, 32 } }, 1.5);

    EXPECT_EQ(windows.caption, (PixelRect{ 0, 0, 1200, 48 }));
    EXPECT_EQ(windows.passthrough,
              (std::vector<PixelRect>{ { 993, 0, 69, 48 }, { 1131, 0, 69, 48 } }));
}

// Told again and again as the window is resized, Windows hears only of a
// change.
TEST(CaptionRegionsTest, WindowsIsToldOnlyOfAChange)
{
    Windows windows;
    CaptionRegions regions{ windows };
    regions.Update(Area{ 0, 0, 800, 32 }, {}, 1);
    regions.Update(Area{ 0, 0, 800, 32 }, {}, 1);
    EXPECT_EQ(windows.told, 1);

    regions.Update(Area{ 0, 0, 900, 32 }, {}, 1);
    EXPECT_EQ(windows.told, 2);
}

// No title bar drawn, or the window's own back: there is no caption, and
// Windows is told so once.
TEST(CaptionRegionsTest, NoTitleBarIsNoCaption)
{
    Windows windows;
    CaptionRegions regions{ windows };
    regions.Update(std::nullopt, {}, 1);
    regions.Update(std::nullopt, {}, 1);
    EXPECT_EQ(windows.told, 1);
    EXPECT_FALSE(windows.caption);

    regions.Update(Area{ 0, 0, 800, 32 }, {}, 1);
    regions.Update(std::nullopt, {}, 1);
    EXPECT_EQ(windows.told, 3);
    EXPECT_FALSE(windows.caption);
}
