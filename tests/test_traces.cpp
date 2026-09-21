#include <gtest/gtest.h>

#include "Session.h"
#include "TraceReader.h"

#include <filesystem>
#include <fstream>
#include <sstream>

// Every trace in tests/traces, played back into a Session.
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

    struct Recorded : urusi::SessionEffects
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

    // Play LINE back into SESSION; say what went wrong, or nothing.
    std::string Play(trace::Line const& line, urusi::Session& session)
    {
        auto event = Get<std::string>(line, "e", "");

        if (event == "activated") session.Activated();
        else if (event == "deactivated") session.Deactivated();
        else if (event == "deactivation-checked")
            session.DeactivationChecked(Get<bool>(line, "foreground", false));
        else if (event == "resume-checked")
            session.ResumeChecked(Get<bool>(line, "foreground", false),
                                  Get<bool>(line, "keys", false));
        else if (event == "focus-gained") session.FocusGained();
        else if (event == "focus-lost") session.FocusLost();
        else if (event == "context-created") session.ContextCreated();
        else if (event == "text-updating")
            session.TextUpdating(Number(line, "start"), Number(line, "end"),
                                 FromUtf8(Get<std::string>(line, "text", "")));
        else if (event == "composition-started") session.CompositionStarted();
        else if (event == "composition-completed") session.CompositionCompleted();
        else if (event == "focus-removed") session.FocusRemoved();
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
        urusi::Session session{ effects };
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
                auto problem = Play(line, session);
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
                EXPECT_EQ(session.Active(), Get<bool>(line, "value", false)) << "line " << number;
            }
            else if (expect == "composing")
            {
                EXPECT_EQ(session.Composing(), Get<bool>(line, "value", false))
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
