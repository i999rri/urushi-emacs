#include <gtest/gtest.h>

#include "Window/DrawReader.h"

using urusi::core::window::DrawFrame;
using urusi::core::window::DrawOp;
using urusi::core::window::DrawReader;

namespace
{
    // The lines of one screen, as hostrecord.c writes them, read into
    // the screen they say.  Nothing comes back until the end arrives.
    std::optional<DrawFrame> ReadAll(DrawReader& reader,
                                     std::initializer_list<char const*> lines)
    {
        std::optional<DrawFrame> screen;

        for (auto const* line : lines)
        {
            if (auto came = reader.Take(line))
            {
                screen = std::move(came);
            }
        }
        return screen;
    }
}

TEST(DrawReaderTest, NothingComesBackUntilTheScreenEnds)
{
    DrawReader reader;

    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"begin","frame":"1a2b","width":800,"height":600})"));
    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"fill","x":1,"y":2,"width":3,"height":4,"color":"#010203"})"));

    auto screen = reader.Take(R"({"type":"draw","op":"end","frame":"1a2b"})");
    ASSERT_TRUE(screen);
    EXPECT_EQ(screen->frame, L"1a2b");
    EXPECT_EQ(screen->width, 800);
    EXPECT_EQ(screen->height, 600);
    ASSERT_EQ(screen->commands.size(), 1u);
}

TEST(DrawReaderTest, AFilledBoxIsItsCornerItsSizeAndItsColor)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"fill","x":8,"y":713,"width":1180,"height":31,"color":"#1e1e1e"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);

    auto const& fill = screen->commands[0];
    EXPECT_EQ(fill.op, DrawOp::Fill);
    EXPECT_EQ(fill.x, 8);
    EXPECT_EQ(fill.y, 713);
    EXPECT_EQ(fill.width, 1180);
    EXPECT_EQ(fill.height, 31);
    EXPECT_EQ(fill.color, 0x1e1e1eu);
}

// A line has no size of its own: it runs from one corner to another.
TEST(DrawReaderTest, ALineRunsFromOneCornerToAnother)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"line","x0":0,"y0":773,"x1":1196,"y1":773,"color":"#3a3a3a"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);

    auto const& line = screen->commands[0];
    EXPECT_EQ(line.op, DrawOp::Line);
    EXPECT_EQ(line.x, 0);
    EXPECT_EQ(line.y, 773);
    EXPECT_EQ(line.width, 1196);
    EXPECT_EQ(line.height, 773);
    EXPECT_EQ(line.color, 0x3a3a3au);
}

TEST(DrawReaderTest, AGlyphRunIsAGlyphAndAPlaceForEachOfThem)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"glyphs","font":3,"size":20.00,"y":48,"color":"#ff6b35","ids":[3,3,3,3,20,3],"xs":[8,18,28,38,48,58]})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);

    auto const& run = screen->commands[0];
    EXPECT_EQ(run.op, DrawOp::Glyphs);
    EXPECT_EQ(run.font, 3);
    EXPECT_DOUBLE_EQ(run.size, 20.0);
    EXPECT_EQ(run.y, 48);
    EXPECT_EQ(run.color, 0xff6b35u);
    EXPECT_EQ(run.ids, (std::vector<uint16_t>{ 3, 3, 3, 3, 20, 3 }));
    EXPECT_EQ(run.xs, (std::vector<int>{ 8, 18, 28, 38, 48, 58 }));
}

// A glyph with no place, or a place with no glyph, is neither: the two
// are read as far as both of them go.
TEST(DrawReaderTest, AGlyphRunIsAsLongAsBothOfItsLists)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"glyphs","font":0,"size":14.00,"y":20,"color":"#ffffff","ids":[7,8,9],"xs":[1,2]})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);
    EXPECT_EQ(screen->commands[0].ids.size(), 2u);
    EXPECT_EQ(screen->commands[0].xs.size(), 2u);
}

TEST(DrawReaderTest, PixelsThatMovedSayWhereTheyWentTo)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"copy","x":0,"y":31,"width":1196,"height":682,"toY":0})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);

    auto const& copy = screen->commands[0];
    EXPECT_EQ(copy.op, DrawOp::Copy);
    EXPECT_EQ(copy.y, 31);
    EXPECT_EQ(copy.height, 682);
    EXPECT_EQ(copy.toY, 0);
}

TEST(DrawReaderTest, ClipAndUnclipComeThroughInOrder)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"clip","x":8,"y":28,"width":1180,"height":31})",
          R"({"type":"draw","op":"unclip"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 2u);
    EXPECT_EQ(screen->commands[0].op, DrawOp::Clip);
    EXPECT_EQ(screen->commands[0].width, 1180);
    EXPECT_EQ(screen->commands[1].op, DrawOp::Unclip);
}

// Order is the whole of it: the text goes over the background filled
// before it, so what came first has to still come first.
TEST(DrawReaderTest, TheCommandsKeepTheOrderTheyCameIn)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"clip","x":0,"y":0,"width":8,"height":6})",
          R"({"type":"draw","op":"fill","x":0,"y":0,"width":8,"height":6,"color":"#000000"})",
          R"({"type":"draw","op":"glyphs","font":0,"size":10.00,"y":5,"color":"#ffffff","ids":[1],"xs":[0]})",
          R"({"type":"draw","op":"unclip"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 4u);
    EXPECT_EQ(screen->commands[0].op, DrawOp::Clip);
    EXPECT_EQ(screen->commands[1].op, DrawOp::Fill);
    EXPECT_EQ(screen->commands[2].op, DrawOp::Glyphs);
    EXPECT_EQ(screen->commands[3].op, DrawOp::Unclip);
}

// A screen whose beginning was lost is not drawn from: what it says
// would be drawn onto the screen before it.
TEST(DrawReaderTest, ALineBeforeAnyBeginningIsRefused)
{
    DrawReader reader;

    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"fill","x":0,"y":0,"width":1,"height":1,"color":"#ffffff"})"));
    EXPECT_FALSE(reader.Why().empty());

    EXPECT_FALSE(reader.Take(R"({"type":"draw","op":"end","frame":"f"})"));
}

TEST(DrawReaderTest, AKindOfDrawingThisHostDoesNotKnowIsSaid)
{
    DrawReader reader;

    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})"));
    EXPECT_TRUE(reader.Why().empty());

    EXPECT_FALSE(reader.Take(R"({"type":"draw","op":"xwidget","x":0})"));
    EXPECT_FALSE(reader.Why().empty());
}

// The fields are read by name, so a host that is sent them in another
// order, or sent ones it has no use for, reads the same screen.
TEST(DrawReaderTest, TheOrderOfTheFieldsDoesNotMatter)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"op":"begin","height":600,"type":"draw","frame":"f","width":800})",
          R"({"color":"#0a0b0c","height":4,"op":"fill","width":3,"x":1,"y":2,"type":"draw"})",
          R"({"op":"end","type":"draw","frame":"f"})" });

    ASSERT_TRUE(screen);
    EXPECT_EQ(screen->width, 800);
    ASSERT_EQ(screen->commands.size(), 1u);
    EXPECT_EQ(screen->commands[0].x, 1);
    EXPECT_EQ(screen->commands[0].y, 2);
    EXPECT_EQ(screen->commands[0].width, 3);
    EXPECT_EQ(screen->commands[0].height, 4);
    EXPECT_EQ(screen->commands[0].color, 0x0a0b0cu);
}

TEST(DrawReaderTest, FieldsThisHostHasNoUseForArePassedOver)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"fill","note":"a string","counts":[1,2,3],"depth":7,"x":5,"y":6,"width":7,"height":8,"color":"#ffffff"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 1u);
    EXPECT_EQ(screen->commands[0].x, 5);
    EXPECT_EQ(screen->commands[0].height, 8);
}

// Emacs draws past the top and the left of the picture where a glyph
// leans out of its row.
TEST(DrawReaderTest, ABoxMayLieAboveOrLeftOfTheScreen)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"fill","x":-4,"y":-2,"width":9,"height":9,"color":"#ffffff"})",
          R"({"type":"draw","op":"glyphs","font":0,"size":10.00,"y":-3,"color":"#ffffff","ids":[5],"xs":[-7]})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 2u);
    EXPECT_EQ(screen->commands[0].x, -4);
    EXPECT_EQ(screen->commands[0].y, -2);
    EXPECT_EQ(screen->commands[1].y, -3);
    EXPECT_EQ(screen->commands[1].xs, (std::vector<int>{ -7 }));
}

TEST(DrawReaderTest, AColorThatIsNoColorIsBlack)
{
    DrawReader reader;
    auto screen = ReadAll(
        reader,
        { R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})",
          R"({"type":"draw","op":"fill","x":0,"y":0,"width":1,"height":1,"color":"#gggggg"})",
          R"({"type":"draw","op":"fill","x":0,"y":0,"width":1,"height":1,"color":"12345"})",
          R"({"type":"draw","op":"end","frame":"f"})" });

    ASSERT_TRUE(screen);
    ASSERT_EQ(screen->commands.size(), 2u);
    EXPECT_EQ(screen->commands[0].color, 0u);
    EXPECT_EQ(screen->commands[1].color, 0u);
}

TEST(DrawReaderTest, ALineThatIsNoDrawingIsSaidAndNothingElseHappens)
{
    DrawReader reader;

    EXPECT_FALSE(reader.Take("not a drawing at all"));
    EXPECT_FALSE(reader.Why().empty());
}

// A screen that never ended is dropped when the next one begins, since
// half a screen drawn is a screen no one drew.
TEST(DrawReaderTest, AScreenThatBeginsAgainLeavesTheHalfOneBehind)
{
    DrawReader reader;

    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})"));
    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"fill","x":0,"y":0,"width":1,"height":1,"color":"#ffffff"})"));
    EXPECT_FALSE(reader.Take(
        R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})"));

    auto screen = reader.Take(R"({"type":"draw","op":"end","frame":"f"})");
    ASSERT_TRUE(screen);
    EXPECT_TRUE(screen->commands.empty());
}
