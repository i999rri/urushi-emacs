// Chunks as a pipe gives them, given back a line at a time.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "Measured.h"
#include "Text/Lines.h"

using urusi::core::text::Lines;
using urusi::tests::kChecked;
using urusi::tests::Measured;

namespace
{
    std::vector<std::string> LinesOf(std::vector<std::string> const& chunks)
    {
        std::vector<std::string> said;
        Lines lines;

        for (auto const& chunk : chunks)
        {
            lines.Take(chunk, [&said](std::string line) { said.push_back(std::move(line)); });
        }
        return said;
    }
}

TEST(LinesTest, AChunkOfWholeLinesIsThoseLines)
{
    EXPECT_EQ(LinesOf({ "one\ntwo\nthree\n" }),
              (std::vector<std::string>{ "one", "two", "three" }));
}

TEST(LinesTest, ALineBrokenAcrossChunksIsOneLine)
{
    EXPECT_EQ(LinesOf({ "on", "e and", " two\n" }),
              (std::vector<std::string>{ "one and two" }));
}

TEST(LinesTest, ALineIsSaidOnlyOnceItHasEnded)
{
    Lines lines;
    std::vector<std::string> said;
    auto take = [&said](std::string line) { said.push_back(std::move(line)); };

    lines.Take("not ended yet", take);
    EXPECT_TRUE(said.empty());
    EXPECT_EQ(lines.Held(), 13u);

    lines.Take(", now it is\n", take);
    EXPECT_EQ(said, (std::vector<std::string>{ "not ended yet, now it is" }));
    EXPECT_EQ(lines.Held(), 0u);
}

TEST(LinesTest, ACarriageReturnBeforeTheNewlineIsNoPartOfTheLine)
{
    EXPECT_EQ(LinesOf({ "one\r\ntwo\r\n" }), (std::vector<std::string>{ "one", "two" }));
    // One in the middle is the line's own, whatever it is doing there.
    EXPECT_EQ(LinesOf({ "a\rb\n" }), (std::vector<std::string>{ "a\rb" }));
}

TEST(LinesTest, AnEmptyLineIsALine)
{
    EXPECT_EQ(LinesOf({ "\n\na\n" }), (std::vector<std::string>{ "", "", "a" }));
}

TEST(LinesTest, TheLastLineWithNoNewlineIsHeldAndNotSaid)
{
    EXPECT_EQ(LinesOf({ "one\ntwo" }), (std::vector<std::string>{ "one" }));
}

TEST(LinesTest, ALineOfAHundredMegabytesCostsWhatItsLengthCosts)
{
    if (kChecked)
    {
        SUCCEED() << "not timed: the checked standard library is not what this runs on";
        return;
    }

    // The shape the file of a font arrives in: one line, in as many
    // reads as a pipe gives.  Looking from the beginning of what is
    // held on every read would be a hundred gigabytes of looking here,
    // which is where the window used to stand still.
    constexpr size_t kChunk = 64 * 1024;
    constexpr size_t kChunks = 1600;
    std::string const chunk(kChunk, 'x');
    std::vector<std::string> said;
    Lines lines;

    auto const began = std::chrono::steady_clock::now();
    for (size_t at = 0; at < kChunks; ++at)
    {
        lines.Take(chunk, [&said](std::string line) { said.push_back(std::move(line)); });
    }
    lines.Take("\n", [&said](std::string line) { said.push_back(std::move(line)); });
    auto const took = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - began).count();

    ASSERT_EQ(said.size(), 1u);
    EXPECT_EQ(said[0].size(), kChunk * kChunks);

    Measured("pipe.line.of.a.hundred.megabytes", took, "milliseconds");

    // Looking at every byte once, and moving what is held only where a
    // line ended, comes to under a tenth of a second on the machine
    // this was written on.  Either done the other way is seven seconds,
    // so anything near a second says one of them has come back.
    EXPECT_LT(took, 1000.0);
}
