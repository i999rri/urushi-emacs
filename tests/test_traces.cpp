#include <gtest/gtest.h>

#include "Keyboard.h"
#include "TraceReader.h"

#include <filesystem>
#include <fstream>
#include <sstream>

// Every trace in tests/traces, played back into a Keyboard.
//
// A trace is what urusi-debug-mode writes beside the application
// (urusi-emacs.trace.jsonl): one line of JSON for each thing that
// happened to the keyboard's side of the window, with "e" saying what.
// Lines with "out" are what was done about it, there to be read; they
// are not played back. Lines with "expect", added by hand, say what the
// playback is to come to:
//
//   {"expect":"commits","texts":["あい"]}   what went to Emacs, in order
//   {"expect":"active","value":true}        whether the window is in front
//   {"expect":"composing","value":false}    whether a composition is open
//   {"expect":"entered","value":true}       whether the input method was
//                                           last told the focus is here
//
// Anything else, a {"note":"..."} for one, is passed over.

namespace
{
    namespace fs = std::filesystem;
    using urusi::FromUtf8;
    using urusi::ToUtf8;

    struct Recorded : urusi::KeyboardEvents, urusi::IKeyInputDevice
    {
        void CheckLater() override {}
        void ResumeLater() override {}
        void TellEmacsFocus(bool) override {}
        void NotifyFocusEnter() override { entered = true; }
        void NotifyFocusLeave() override { entered = false; }
        void Commit(std::wstring const& text) override { commits.push_back(ToUtf8(text)); }
        void Composing(std::wstring const&) override {}

        std::vector<std::string> commits;
        bool entered{ false };
    };

    template <typename T>
    T Get(trace::Line const& line, char const* key, T fallback)
    {
        auto found = line.find(key);
        if (found == line.end() || !std::holds_alternative<T>(found->second))
        {
            return fallback;
        }
        return std::get<T>(found->second);
    }

    int32_t Number(trace::Line const& line, char const* key)
    {
        return static_cast<int32_t>(Get<double>(line, key, 0));
    }

    // Play LINE back into KEYBOARD; say what went wrong, or nothing.
    std::string Play(trace::Line const& line, urusi::Keyboard& keyboard)
    {
        auto event = Get<std::string>(line, "e", "");

        if (event == "activated") keyboard.Activated();
        else if (event == "key")
            keyboard.Key({ .key = Number(line, "key"), .menuDown = Get<bool>(line, "alt", false),
                          .down = Get<bool>(line, "down", true) });
        else if (event == "deactivated") keyboard.Deactivated();
        else if (event == "deactivation-checked")
            keyboard.DeactivationChecked(Get<bool>(line, "foreground", false));
        else if (event == "resume-checked")
            keyboard.ResumeChecked(Get<bool>(line, "foreground", false),
                                  Get<bool>(line, "keys", false));
        else if (event == "focus-gained") keyboard.FocusGained();
        else if (event == "focus-lost") keyboard.FocusLost();
        else if (event == "context-created") keyboard.ContextCreated();
        else if (event == "text-updating")
            keyboard.TextUpdating(Number(line, "start"), Number(line, "end"),
                                 FromUtf8(Get<std::string>(line, "text", "")));
        else if (event == "composition-started") keyboard.CompositionStarted();
        else if (event == "composition-completed") keyboard.CompositionCompleted();
        else if (event == "focus-removed") keyboard.FocusRemoved();
        else return "an event that is not known: " + event;
        return {};
    }

    // Beside this file: MSBuild compiles it by its whole path, which is
    // what __FILE__ then is.
    fs::path Traces()
    {
        return fs::path{ __FILE__ }.parent_path() / "traces";
    }
}

// What a keyboard writes down is what a playback reads: a trace taken
// from the application plays back to what happened there.
TEST(TraceTest, WhatIsWrittenPlaysBackTheSame)
{
    Recorded original;
    urusi::Keyboard keyboard{ original, original };
    std::vector<std::string> lines;
    keyboard.Record([&](std::string const& line) { lines.push_back(line); });

    keyboard.ContextCreated();
    keyboard.Key({ .key = 0x41, .down = true });
    keyboard.FocusGained();
    keyboard.Activated();
    keyboard.CompositionStarted();
    keyboard.TextUpdating(0, 0, L"\"か\"\\");
    keyboard.CompositionCompleted();
    keyboard.TextUpdating(3, 3, L"\U0001F600");
    keyboard.Deactivated();
    keyboard.DeactivationChecked(false);

    Recorded played;
    urusi::Keyboard playback{ played, played };
    for (auto const& text : lines)
    {
        trace::Line line;
        trace::Reader reader{ text };
        ASSERT_TRUE(reader.Object(line)) << text;
        if (line.count("e"))
        {
            EXPECT_EQ(Play(line, playback), "") << text;
        }
    }

    EXPECT_EQ(played.commits, original.commits);
    EXPECT_EQ(played.commits, (std::vector<std::string>{ ToUtf8(L"\"か\"\\"),
                                                         ToUtf8(L"\U0001F600") }));
    EXPECT_EQ(playback.Active(), keyboard.Active());
}

// Nobody listening, nothing is written.
TEST(TraceTest, NothingIsMadeWithoutARecorder)
{
    Recorded effects;
    urusi::Keyboard keyboard{ effects, effects };
    std::vector<std::string> lines;
    keyboard.Record([&](std::string const& line) { lines.push_back(line); });
    keyboard.Record({});

    keyboard.Activated();
    keyboard.TextUpdating(0, 0, L"a");
    EXPECT_TRUE(lines.empty());
}

TEST(TraceTest, EveryTracePlaysBackToWhatItExpects)
{
    ASSERT_TRUE(fs::is_directory(Traces())) << Traces();

    int played = 0;
    for (auto const& entry : fs::directory_iterator(Traces()))
    {
        if (entry.path().extension() != ".jsonl")
        {
            continue;
        }
        SCOPED_TRACE(entry.path().filename().string());
        ++played;

        Recorded effects;
        urusi::Keyboard keyboard{ effects, effects };
        std::ifstream file{ entry.path(), std::ios::binary };
        std::string text;
        int number = 0;
        int expectations = 0;

        while (std::getline(file, text))
        {
            ++number;
            if (!text.empty() && text.back() == '\r')
            {
                text.pop_back();
            }
            if (text.empty())
            {
                continue;
            }

            trace::Line line;
            trace::Reader reader{ text };
            ASSERT_TRUE(reader.Object(line)) << "line " << number << " is not understood";

            if (line.count("e"))
            {
                auto problem = Play(line, keyboard);
                EXPECT_TRUE(problem.empty()) << "line " << number << ": " << problem;
                continue;
            }

            auto expect = Get<std::string>(line, "expect", "");
            if (expect.empty())
            {
                continue;
            }
            ++expectations;

            if (expect == "commits")
            {
                EXPECT_EQ(effects.commits, Get<std::vector<std::string>>(line, "texts", {}))
                    << "line " << number;
            }
            else if (expect == "active")
            {
                EXPECT_EQ(keyboard.Active(), Get<bool>(line, "value", false)) << "line " << number;
            }
            else if (expect == "composing")
            {
                EXPECT_EQ(keyboard.Composing(), Get<bool>(line, "value", false))
                    << "line " << number;
            }
            else if (expect == "entered")
            {
                EXPECT_EQ(effects.entered, Get<bool>(line, "value", false)) << "line " << number;
            }
            else
            {
                ADD_FAILURE() << "line " << number << ": an expectation that is not known: "
                              << expect;
            }
        }

        EXPECT_GT(expectations, 0) << "a trace that expects nothing tests nothing";
    }

    EXPECT_GT(played, 0) << "no traces in " << Traces();
}
