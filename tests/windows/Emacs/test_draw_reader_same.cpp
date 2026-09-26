// That the reader written by hand reads what the one it replaced read.
//
// The host used to read each line of a screen into an object of its
// own, with Windows.Data.Json; it now reads the bytes as they come,
// because building an object for every field of every line cost as
// much processor time as the drawing did.  That is a hand-written
// reader in place of one that was not, so the one it replaced is kept
// here and both are given the same screen -- a real one, recorded in
// tests/traces -- and held to the same answer.
//
// Nothing here is in the application: this is the reader as it was,
// kept only to hold the reader as it is to it.

#include <gtest/gtest.h>

// The collections come first: what a JSON array answers to is
// defined there, and using it before that is an error.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Measured.h"
#include "Window/DrawReader.h"

using winrt::Windows::Data::Json::JsonObject;
using urusi::tests::kChecked;
using urusi::tests::Measured;
using urusi::core::window::DrawCommand;
using urusi::core::window::DrawFrame;
using urusi::core::window::DrawOp;

namespace
{
    namespace fs = std::filesystem;

    // The reader as it was.
    namespace was
    {
        uint32_t ColorOf(JsonObject const& said)
        {
            auto text = said.GetNamedString(L"color", L"");
            if (text.size() != 7 || text[0] != L'#')
            {
                return 0;
            }
            return static_cast<uint32_t>(
                std::wcstoul(text.c_str() + 1, nullptr, 16));
        }

        int NumberOf(JsonObject const& said, wchar_t const* name)
        {
            return static_cast<int>(said.GetNamedNumber(name, 0));
        }

        void ReadGlyphs(JsonObject const& said, DrawCommand& command)
        {
            command.font = NumberOf(said, L"font");
            command.size = said.GetNamedNumber(L"size", 0);
            command.y = NumberOf(said, L"y");
            command.color = ColorOf(said);

            auto ids = said.GetNamedArray(L"ids", nullptr);
            auto xs = said.GetNamedArray(L"xs", nullptr);
            if (!ids || !xs)
            {
                return;
            }

            auto count = (std::min)(ids.Size(), xs.Size());
            command.ids.reserve(count);
            command.xs.reserve(count);
            for (uint32_t at = 0; at < count; ++at)
            {
                command.ids.push_back(static_cast<uint16_t>(ids.GetNumberAt(at)));
                command.xs.push_back(static_cast<int>(xs.GetNumberAt(at)));
            }
        }

        class Reader
        {
        public:
            std::optional<DrawFrame> Take(JsonObject const& said)
            {
                auto op = said.GetNamedString(L"op", L"");

                if (op == L"begin")
                {
                    m_frame = DrawFrame{};
                    m_frame.frame = said.GetNamedString(L"frame", L"");
                    m_frame.width = NumberOf(said, L"width");
                    m_frame.height = NumberOf(said, L"height");
                    m_begun = true;
                    return std::nullopt;
                }

                if (!m_begun)
                {
                    return std::nullopt;
                }

                if (op == L"end")
                {
                    m_begun = false;
                    return std::move(m_frame);
                }

                DrawCommand command;
                if (op == L"fill" || op == L"rectangle" || op == L"clip")
                {
                    command.op = op == L"fill"        ? DrawOp::Fill
                                 : op == L"rectangle" ? DrawOp::Rectangle
                                                      : DrawOp::Clip;
                    command.x = NumberOf(said, L"x");
                    command.y = NumberOf(said, L"y");
                    command.width = NumberOf(said, L"width");
                    command.height = NumberOf(said, L"height");
                    command.color = ColorOf(said);
                }
                else if (op == L"line")
                {
                    command.op = DrawOp::Line;
                    command.x = NumberOf(said, L"x0");
                    command.y = NumberOf(said, L"y0");
                    command.width = NumberOf(said, L"x1");
                    command.height = NumberOf(said, L"y1");
                    command.color = ColorOf(said);
                }
                else if (op == L"copy")
                {
                    command.op = DrawOp::Copy;
                    command.x = NumberOf(said, L"x");
                    command.y = NumberOf(said, L"y");
                    command.width = NumberOf(said, L"width");
                    command.height = NumberOf(said, L"height");
                    command.toY = NumberOf(said, L"toY");
                }
                else if (op == L"unclip")
                {
                    command.op = DrawOp::Unclip;
                }
                else if (op == L"glyphs")
                {
                    command.op = DrawOp::Glyphs;
                    ReadGlyphs(said, command);
                }
                else
                {
                    return std::nullopt;
                }

                m_frame.commands.push_back(std::move(command));
                return std::nullopt;
            }

        private:
            DrawFrame m_frame;
            bool m_begun{ false };
        };
    }

    // tests/screens, three directories up from this file.  Apart from
    // tests/traces, which is played back into a keyboard: a screen is
    // not that and would be read as one.
    fs::path Screens()
    {
        return fs::path{ __FILE__ }
                   .parent_path()
                   .parent_path()
                   .parent_path()
               / "screens";
    }

    std::vector<std::string> Screen()
    {
        std::vector<std::string> lines;
        std::ifstream file{ Screens() / "draw-screen.jsonl" };
        std::string line;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                lines.push_back(line);
            }
        }
        return lines;
    }

    void Same(DrawCommand const& one, DrawCommand const& other, size_t at)
    {
        EXPECT_EQ(one.op, other.op) << "command " << at;
        EXPECT_EQ(one.x, other.x) << "command " << at;
        EXPECT_EQ(one.y, other.y) << "command " << at;
        EXPECT_EQ(one.width, other.width) << "command " << at;
        EXPECT_EQ(one.height, other.height) << "command " << at;
        EXPECT_EQ(one.toY, other.toY) << "command " << at;
        EXPECT_EQ(one.color, other.color) << "command " << at;
        EXPECT_EQ(one.font, other.font) << "command " << at;
        EXPECT_DOUBLE_EQ(one.size, other.size) << "command " << at;
        EXPECT_EQ(one.ids, other.ids) << "command " << at;
        EXPECT_EQ(one.xs, other.xs) << "command " << at;
    }
}

// A whole screen, as Emacs drew one, read by both.
TEST(DrawReaderSameTest, BothReadersMakeTheSameScreenOfARealOne)
{
    auto const lines = Screen();
    ASSERT_FALSE(lines.empty()) << "no screen in " << Screens();

    urusi::core::window::DrawReader now;
    was::Reader before;
    std::optional<DrawFrame> ours;
    std::optional<DrawFrame> theirs;

    for (auto const& line : lines)
    {
        if (auto came = now.Take(line))
        {
            ours = std::move(came);
        }

        JsonObject said{ nullptr };
        ASSERT_TRUE(JsonObject::TryParse(winrt::to_hstring(line), said))
            << "a line that is no JSON: " << line.substr(0, 120);
        if (auto came = before.Take(said))
        {
            theirs = std::move(came);
        }
    }

    ASSERT_TRUE(ours) << "the screen never ended";
    ASSERT_TRUE(theirs);

    EXPECT_EQ(ours->width, theirs->width);
    EXPECT_EQ(ours->height, theirs->height);
    ASSERT_EQ(ours->commands.size(), theirs->commands.size());

    for (size_t at = 0; at < ours->commands.size(); ++at)
    {
        Same(ours->commands[at], theirs->commands[at], at);
    }
}

// The shapes that a recorded screen may not have in it, put to both by
// hand.
TEST(DrawReaderSameTest, BothReadersMakeTheSameOfTheShapesAScreenMayNotHave)
{
    std::vector<std::string> const lines{
        R"({"type":"draw","op":"begin","frame":"f","width":640,"height":480})",
        R"({"type":"draw","op":"rectangle","x":3,"y":4,"width":5,"height":6,"color":"#0a141e"})",
        R"({"type":"draw","op":"line","x0":0,"y0":7,"x1":640,"y1":7,"color":"#ffffff"})",
        R"({"type":"draw","op":"copy","x":0,"y":31,"width":640,"height":400,"toY":0})",
        R"({"type":"draw","op":"fill","x":-4,"y":-2,"width":9,"height":9,"color":"#010203"})",
        R"({"type":"draw","op":"glyphs","font":2,"size":13.50,"y":-3,"color":"#abcdef","ids":[65535,0,7],"xs":[-7,0,1000]})",
        R"({"op":"fill","height":4,"width":3,"y":2,"x":1,"color":"#0a0b0c","type":"draw"})",
        R"({"type":"draw","op":"end","frame":"f"})",
    };

    urusi::core::window::DrawReader now;
    was::Reader before;
    std::optional<DrawFrame> ours;
    std::optional<DrawFrame> theirs;

    for (auto const& line : lines)
    {
        if (auto came = now.Take(line))
        {
            ours = std::move(came);
        }

        JsonObject said{ nullptr };
        ASSERT_TRUE(JsonObject::TryParse(winrt::to_hstring(line), said));
        if (auto came = before.Take(said))
        {
            theirs = std::move(came);
        }
    }

    ASSERT_TRUE(ours);
    ASSERT_TRUE(theirs);
    ASSERT_EQ(ours->commands.size(), theirs->commands.size());

    for (size_t at = 0; at < ours->commands.size(); ++at)
    {
        Same(ours->commands[at], theirs->commands[at], at);
    }
}

// How long each of them takes over the same screen.
//
// What each takes from the heap would say it better, but cannot be
// counted here: Windows.Data.Json is a runtime of the system's, and
// what it takes it takes from a heap of its own, which the counting
// this test program does of `operator new' never sees.  Counting it
// would say the reader that was takes nothing, which is the opposite
// of why it was replaced.  Time is what both are on the same footing
// for.
TEST(DrawReaderSameTest, TheHandWrittenReaderIsTheFasterOfTheTwo)
{
    if (kChecked)
    {
        SUCCEED() << "not timed: the checked standard library is not what"
                     " either of these runs on";
        return;
    }

    auto const lines = Screen();
    ASSERT_FALSE(lines.empty()) << "no screen in " << Screens();

    constexpr int kTimes = 50;

    auto const began = std::chrono::steady_clock::now();
    for (int time = 0; time < kTimes; ++time)
    {
        urusi::core::window::DrawReader now;

        for (auto const& line : lines)
        {
            now.Take(line);
        }
    }
    auto const ours = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - began)
                          .count()
                      / static_cast<double>(kTimes);

    auto const alsoBegan = std::chrono::steady_clock::now();
    for (int time = 0; time < kTimes; ++time)
    {
        was::Reader before;

        for (auto const& line : lines)
        {
            JsonObject said{ nullptr };

            if (JsonObject::TryParse(winrt::to_hstring(line), said))
            {
                before.Take(said);
            }
        }
    }
    auto const theirs = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - alsoBegan)
                            .count()
                        / static_cast<double>(kTimes);

    Measured("screen.read.by.hand", ours, "microseconds");
    Measured("screen.read.into.objects", theirs, "microseconds");
    Measured("screen.read.times.faster", theirs / (ours > 0 ? ours : 1), "times");

    EXPECT_LT(ours, theirs) << ours << " against " << theirs
                            << " microseconds a screen";
}

namespace
{
    // One run of HOW_MANY glyphs, as Emacs writes one.
    std::string RunOfGlyphs(int how_many)
    {
        std::string ids;
        std::string xs;

        for (int at = 0; at < how_many; ++at)
        {
            if (at)
            {
                ids += ",";
                xs += ",";
            }
            ids += std::to_string(3 + at % 90);
            xs += std::to_string(8 + at * 10);
        }
        return R"({"type":"draw","op":"glyphs","font":3,"size":20.00,"y":48,)"
               R"("color":"#ff6b35","ids":[)"
               + ids + R"(],"xs":[)" + xs + "]}";
    }

    template <typename Read>
    double Each(std::string const& line, int how_many, Read read)
    {
        constexpr int kTimes = 2000;
        auto const began = std::chrono::steady_clock::now();

        for (int time = 0; time < kTimes; ++time)
        {
            read(line);
        }

        auto const took = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - began)
                              .count();
        return static_cast<double>(took) / kTimes / how_many;
    }
}

// What one glyph of a run costs each of them.
//
// This is what the reader was written by hand for.  The one before it
// read a line into an object of its own, and every number in the line
// became an object as well: a run of a hundred glyphs carries two
// hundred numbers, so two hundred objects made, counted and freed.
//
// A run is read at three lengths, so that what is done once for the
// line is told apart from what is done for each number in it.  Both
// get cheaper for each glyph as the run grows, since both have a cost
// for the line; what the longest run shows is the floor, which is what
// one number costs.  Measured on the machine this was written on, in
// nanoseconds for each glyph:
//
//               by hand   into objects
//     1 glyph     500        25000
//    10 glyphs     62         3600
//   100 glyphs     20         1500
//
// Twenty against fifteen hundred is the object each number is made
// into.  It is that floor, not the reading of the text, that put the
// thread reading from Emacs at nine per cent of a processor while it
// drew -- as much as the drawing itself -- and taking it away is what
// the hand-written reader is for.
TEST(DrawReaderSameTest, TheReaderThatWasPaysForEveryNumberOfARun)
{
    if (kChecked)
    {
        SUCCEED() << "not timed: the checked standard library is not what"
                     " either of these runs on";
        return;
    }

    struct { int many; double ours; double theirs; } at[] = {
        { 1, 0, 0 }, { 10, 0, 0 }, { 100, 0, 0 }
    };

    for (auto& length : at)
    {
        auto const line = RunOfGlyphs(length.many);

        length.ours = Each(line, length.many, [](std::string const& said) {
            urusi::core::window::DrawReader reader;

            reader.Take(
                R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})");
            reader.Take(said);
        });

        length.theirs = Each(line, length.many, [](std::string const& said) {
            was::Reader reader;
            JsonObject object{ nullptr };

            reader.Take(JsonObject::Parse(
                winrt::to_hstring(
                    R"({"type":"draw","op":"begin","frame":"f","width":8,"height":6})")));
            if (JsonObject::TryParse(winrt::to_hstring(said), object))
            {
                reader.Take(object);
            }
        });

        std::cout << "a run of " << length.many << " glyphs: " << length.ours
                  << " nanoseconds a glyph by hand, " << length.theirs
                  << " into objects\n";
    }

    Measured("glyph.by.hand", at[2].ours, "nanoseconds");
    Measured("glyph.into.objects", at[2].theirs, "nanoseconds");

    // Both spread the cost of the line over a longer run.
    EXPECT_LT(at[2].ours, at[0].ours)
        << at[0].ours << " a glyph in a run of one, " << at[2].ours
        << " in a run of a hundred";
    EXPECT_LT(at[2].theirs, at[0].theirs);

    // What is left at the bottom is what one number costs, and for one
    // that is made into an object of its own it is another order
    // altogether.  Ten times is far under what was measured, so only
    // something gone badly wrong trips this.
    EXPECT_GT(at[2].theirs, at[2].ours * 10)
        << at[2].theirs << " against " << at[2].ours
        << " nanoseconds a glyph in a run of a hundred";
}
