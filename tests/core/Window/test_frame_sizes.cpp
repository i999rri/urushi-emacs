#include <gtest/gtest.h>

#include "Window/FrameSizes.h"

using urusi::core::window::FrameSizes;
using urusi::core::window::PixelRect;
using urusi::core::window::PixelSize;
using urusi::core::window::ToPixels;

TEST(FrameSizesTest, XamlUnitsBecomeThePixelsOfTheScreen)
{
    EXPECT_EQ(ToPixels(100, 1.5), 150);
    EXPECT_EQ(ToPixels(10.4, 1.25), 13);
    EXPECT_EQ(ToPixels(10, 20, 30.5, 40, 2), (PixelRect{ 20, 40, 61, 80 }));
}

TEST(FrameSizesTest, AFrameIsToldItsSizeOnceUntilItChanges)
{
    FrameSizes sizes;
    EXPECT_EQ(sizes.Resized(L"", 800, 600, 1.5), (PixelSize{ 1200, 900 }));
    EXPECT_FALSE(sizes.Resized(L"", 800, 600, 1.5));
    EXPECT_EQ(sizes.Resized(L"", 800, 500, 1.5), (PixelSize{ 1200, 750 }));
}

// The window's frame and the frames of panels are told apart.
TEST(FrameSizesTest, EachFrameIsToldOfItsOwn)
{
    FrameSizes sizes;
    EXPECT_TRUE(sizes.Resized(L"", 800, 600, 1));
    EXPECT_TRUE(sizes.Resized(L"output", 800, 600, 1));
    EXPECT_FALSE(sizes.Resized(L"output", 800, 600, 1));
}

// An element not laid out yet has no size, and a frame given none would
// have no room to lay its text out in.
TEST(FrameSizesTest, NoSizeIsNotTold)
{
    FrameSizes sizes;
    EXPECT_FALSE(sizes.Resized(L"", 0, 600, 1));
    EXPECT_FALSE(sizes.Resized(L"", 800, 0.2, 1));
}
