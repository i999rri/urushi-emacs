// What reading a screen costs, which is why it is read the way it is.
//
// The reader this replaced built an object for every field of every
// line.  A screen is a few hundred lines and is drawn many times a
// second, and that came to as much processor time as the drawing
// itself: nine per cent of a thread against the drawing's nine.  So
// these count what the reader takes from the heap and how long it
// takes, and a change that quietly puts either back is a test that
// fails rather than a window that feels slow.
//
// Each run writes what it measured beside the test program and says
// what the run before it measured, so that two runs can be put beside
// each other without anyone writing the numbers down.
//
// Only where the standard library is not the checked one: it takes a
// piece of the heap for every container made, which is ten times what
// the reader itself takes and would be the whole of what was counted.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "Measured.h"
#include "Window/DrawReader.h"

using urusi::core::window::DrawReader;
using urusi::tests::Measured;

namespace
{
    std::atomic<size_t> g_taken{ 0 };
    std::atomic<bool> g_counting{ false };

    // Counts what is taken from the heap while it is alive.
    struct Counting
    {
        Counting()
        {
            g_taken = 0;
            g_counting = true;
        }
        ~Counting() { g_counting = false; }

        size_t Taken() const { return g_taken.load(); }
    };

    constexpr bool kChecked =
#if defined _ITERATOR_DEBUG_LEVEL && _ITERATOR_DEBUG_LEVEL > 0
        true;
#else
        false;
#endif

}

void* operator new(size_t size)
{
    if (g_counting.load())
    {
        ++g_taken;
    }
    if (void* block = std::malloc(size ? size : 1))
    {
        return block;
    }
    throw std::bad_alloc{};
}

void operator delete(void* block) noexcept
{
    std::free(block);
}

void operator delete(void* block, size_t) noexcept
{
    std::free(block);
}

namespace
{
    constexpr char const* kBegin
        = R"({"type":"draw","op":"begin","frame":"f","width":1196,"height":800})";
    constexpr char const* kEnd = R"({"type":"draw","op":"end","frame":"f"})";
    constexpr char const* kFill
        = R"({"type":"draw","op":"fill","x":8,"y":713,"width":1180,"height":31,"color":"#1e1e1e"})";
    constexpr char const* kClip
        = R"({"type":"draw","op":"clip","x":8,"y":28,"width":1180,"height":31})";
    constexpr char const* kUnclip = R"({"type":"draw","op":"unclip"})";
    constexpr char const* kGlyphs
        = R"({"type":"draw","op":"glyphs","font":3,"size":20.00,"y":48,"color":"#ff6b35","ids":[3,3,3,3,20,3,7,8,9,10],"xs":[8,18,28,38,48,58,68,78,88,98]})";
}

// A line that is not a run of glyphs holds nothing of its own, so
// reading one takes nothing from the heap but the room for the growing
// list of commands, which doubles and so asks a handful of times for a
// thousand of them.
TEST(DrawReaderCostTest, ALineOfPlainDrawingCostsAlmostNothing)
{
    if (kChecked)
    {
        SUCCEED() << "not counted: the checked standard library takes a"
                     " piece of the heap for every container it makes, which"
                     " is not what this is about";
        return;
    }

    DrawReader reader;
    constexpr int kLines = 1000;

    reader.Take(kBegin);

    Counting counting;
    for (int at = 0; at < kLines; ++at)
    {
        reader.Take(at % 2 ? kFill : kClip);
    }
    auto const taken = counting.Taken();

    Measured("plain.heap.per.thousand", static_cast<double>(taken), "pieces");
    EXPECT_LT(taken, 40u) << taken << " from the heap for " << kLines
                          << " lines";
}

// A run of glyphs holds two lists, and takes the room for each of them
// once: the numbers are counted before they are read, so neither list
// grows a piece at a time.
TEST(DrawReaderCostTest, ARunOfGlyphsCostsTwoPiecesOfHeapAndNoMore)
{
    if (kChecked)
    {
        SUCCEED() << "not counted: the checked standard library counts"
                     " its own";
        return;
    }

    DrawReader reader;
    constexpr int kLines = 1000;

    reader.Take(kBegin);

    Counting counting;
    for (int at = 0; at < kLines; ++at)
    {
        reader.Take(kGlyphs);
    }
    auto const taken = counting.Taken();

    Measured("glyphs.heap.per.thousand", static_cast<double>(taken), "pieces");
    // Two for each line, and the growing list of commands besides.
    EXPECT_LE(taken, 2u * kLines + 40u)
        << taken << " from the heap for " << kLines << " runs of ten glyphs";
    EXPECT_GE(taken, 2u * kLines) << "the lists have to be somewhere";
}

// Nothing is kept from a line once it has been read, so reading screen
// after screen costs the same each time.
TEST(DrawReaderCostTest, ReadingScreenAfterScreenDoesNotGrow)
{
    if (kChecked)
    {
        SUCCEED() << "not counted: the checked standard library counts"
                     " its own";
        return;
    }

    DrawReader reader;
    constexpr int kScreens = 20;
    size_t first = 0;
    size_t last = 0;

    for (int screen = 0; screen < kScreens; ++screen)
    {
        Counting counting;

        reader.Take(kBegin);
        for (int at = 0; at < 100; ++at)
        {
            reader.Take(kFill);
            reader.Take(kGlyphs);
        }
        reader.Take(kEnd);

        if (screen == 0)
        {
            first = counting.Taken();
        }
        last = counting.Taken();
    }

    EXPECT_LE(last, first) << first << " from the heap for the first screen, "
                           << last << " for the last";
}

// How long a whole screen takes to read.  The bound is loose, since a
// machine under load would fail a tight one; the number beside the run
// before it is what says whether anything changed.
TEST(DrawReaderCostTest, AScreenIsReadInWellUnderAFrame)
{
    if (kChecked)
    {
        SUCCEED() << "not timed: the checked standard library is not what"
                     " this runs on";
        return;
    }

    // A screen of text comes to about this: a full one was measured at
    // 438 lines, 143 of them fills and 63 runs of glyphs.
    std::vector<std::string> screen;

    screen.emplace_back(kBegin);
    for (int at = 0; at < 143; ++at)
    {
        screen.emplace_back(kFill);
    }
    for (int at = 0; at < 116; ++at)
    {
        screen.emplace_back(kClip);
        screen.emplace_back(kUnclip);
    }
    for (int at = 0; at < 63; ++at)
    {
        screen.emplace_back(kGlyphs);
    }
    screen.emplace_back(kEnd);

    constexpr int kTimes = 200;
    DrawReader reader;
    auto const began = std::chrono::steady_clock::now();

    for (int time = 0; time < kTimes; ++time)
    {
        for (auto const& line : screen)
        {
            reader.Take(line);
        }
    }

    auto const took = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - began)
                          .count();
    double const each = static_cast<double>(took) / kTimes;

    Measured("screen.read", each, "microseconds");
    // A screen is drawn at most sixty times a second, so a millisecond
    // a screen would still leave the reading at a few per cent of a
    // thread; the reader this replaced was at nine.
    EXPECT_LT(each, 1000.0) << each << " microseconds a screen";
}
