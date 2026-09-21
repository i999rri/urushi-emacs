#include <gtest/gtest.h>

#include "Splitter.h"

using urusi::DragSplitter;
using urusi::ParseSplitter;

TEST(SplitterTest, NameSaysTheWayAndThePartsEitherSide)
{
    auto side = ParseSplitter(L"urusi-splitter:h:explorer:editor");
    ASSERT_TRUE(side);
    EXPECT_TRUE(side->horizontal);
    EXPECT_EQ(side->before, L"explorer");
    EXPECT_EQ(side->after, L"editor");

    auto stacked = ParseSplitter(L"urusi-splitter:v:editor:output");
    ASSERT_TRUE(stacked);
    EXPECT_FALSE(stacked->horizontal);
}

TEST(SplitterTest, OtherNamesAreNotSplitters)
{
    EXPECT_FALSE(ParseSplitter(L"urusi-frame"));
    EXPECT_FALSE(ParseSplitter(L"urusi-splitter:"));
    EXPECT_FALSE(ParseSplitter(L"urusi-splitter:x:a:b"));
}

// The editor shares what is left, the output panel has a size of its
// own: dragging the splitter between them changes the output's.
TEST(SplitterTest, ThePartWithASizeOfItsOwnIsTheOneResized)
{
    auto up = DragSplitter(500, 200, -50, true, false);
    EXPECT_FALSE(up.before);
    EXPECT_EQ(up.length, 250);

    auto panel = DragSplitter(260, 800, 40, false, true);
    EXPECT_TRUE(panel.before);
    EXPECT_EQ(panel.length, 300);
}

TEST(SplitterTest, NeitherWithASizeGivesTheOneBeforeOne)
{
    auto resize = DragSplitter(300, 300, 20, true, true);
    EXPECT_TRUE(resize.before);
    EXPECT_EQ(resize.length, 320);
}

TEST(SplitterTest, NoPartIsMadeShorterThanTheLeast)
{
    EXPECT_EQ(DragSplitter(100, 100, -500, false, false).length, 40);
    EXPECT_EQ(DragSplitter(100, 100, 500, false, false).length, 160);
    EXPECT_EQ(DragSplitter(500, 100, 500, true, false).length, 40);
}

// Both already shorter than the least between them: nothing moves, and
// nothing asks the impossible of std::clamp.
TEST(SplitterTest, PartsWithNoRoomDoNotMove)
{
    auto resize = DragSplitter(30, 30, 10, false, false);
    EXPECT_TRUE(resize.before);
    EXPECT_EQ(resize.length, 30);
}
