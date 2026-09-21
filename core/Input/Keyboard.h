#pragma once

#include "Input/Activation.h"
#include "Input/Engagement.h"
#include "Input/ImeBuffer.h"
#include "Input/KeyInput.h"
#include "Text/Utf.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace urusi::core::input
{
    // What the keyboard tells the window it is in: the things that
    // touch Windows, or Emacs.
    struct KeyboardEvents
    {
        virtual ~KeyboardEvents() = default;

        // Look later, once XAML is done, whether the window is still in
        // front, and say so with DeactivationChecked.
        virtual void CheckLater() = 0;

        // Look later, once XAML has put the focus back, whether the
        // keys come here, and say so with ResumeChecked.
        virtual void ResumeLater() = 0;

        // Tell Emacs its frame has the focus, or has lost it.
        virtual void TellEmacsFocus(bool focused) = 0;

        // What the input method settled on, to go to Emacs as typed.
        virtual void Commit(std::wstring const& text) = 0;

        // What it is still turning over, to be drawn at the cursor;
        // empty once there is nothing.
        virtual void Composing(std::wstring const& text) = 0;
    };

    // The keyboard's side of the window: which window has it, what
    // becomes of each key, and what the input method makes of them.
    // Everything that happens to it comes in through one of these calls,
    // and everything it does goes out to the device the keys come from
    // (IKeyInputDevice) or to the window (KeyboardEvents), so that it can
    // be written down as it happens and played back in a test to happen
    // again.
    //
    // UI thread only.
    class Keyboard
    {
    public:
        // Writes one line of JSON for each thing that happens.
        using Recorder = std::function<void(std::string const&)>;

        Keyboard(IKeyInputDevice& device, KeyboardEvents& events)
            : m_device(device), m_effects(events)
        {
        }

        void Record(Recorder recorder) { m_recorder = std::move(recorder); }

        // ----- The window -----

        void Activated()
        {
            Say(R"({"e":"activated"})");
            Perform(m_activation.Activated());
        }

        void Deactivated()
        {
            Say(R"({"e":"deactivated"})");
            Perform(m_activation.Deactivated());
        }

        void DeactivationChecked(bool foreground)
        {
            Say([&] {
                return std::string{ R"({"e":"deactivation-checked","foreground":)" }
                       + Bool(foreground) + "}";
            });
            Perform(m_activation.DeactivationChecked(foreground));
        }

        void ResumeChecked(bool foreground, bool keysComeHere)
        {
            Say([&] {
                return std::string{ R"({"e":"resume-checked","foreground":)" }
                       + Bool(foreground) + R"(,"keys":)" + Bool(keysComeHere) + "}";
            });
            Perform(m_activation.ResumeChecked(foreground, keysComeHere));
        }

        // ----- The keys -----

        // A key was pressed or let go. What it becomes for Emacs is the
        // platform's to say; it is written down here, beside what the
        // input method made of it.
        void Key(KeyEvent const& key)
        {
            Say([&] {
                return std::string{ R"({"e":"key","key":)" } + std::to_string(key.key)
                       + R"(,"down":)" + Bool(key.down) + R"(,"alt":)" + Bool(key.menuDown)
                       + "}";
            });
        }

        // ----- The element the keys go to -----

        // It has the focus, or has lost it, as XAML says.
        void FocusGained()
        {
            Say(R"({"e":"focus-gained"})");
            Want(true);
        }

        void FocusLost()
        {
            Say(R"({"e":"focus-lost"})");
            Want(false);
        }

        // ----- The input method -----

        void ContextCreated()
        {
            Say(R"({"e":"context-created"})");
            Tell(m_engagement.ContextCreated());
        }

        void TextUpdating(int32_t start, int32_t end, std::wstring const& text)
        {
            Say([&] {
                return std::string{ R"({"e":"text-updating","start":)" } + std::to_string(start)
                       + R"(,"end":)" + std::to_string(end) + R"(,"text":)" + Quote(text) + "}";
            });
            m_buffer.Update(start, end, text);

            // Outside a composition there is nothing to turn over: what
            // arrives is what was meant, as when a key is typed with the
            // input method open but idle.
            if (m_buffer.Composing())
            {
                Say([&] {
                    return R"({"out":"composing","text":)" + Quote(m_buffer.Composed()) + "}";
                });
                m_effects.Composing(m_buffer.Composed());
            }
            else
            {
                Settle();
            }
        }

        void CompositionStarted()
        {
            Say(R"({"e":"composition-started"})");
            m_buffer.Started();
        }

        void CompositionCompleted()
        {
            Say(R"({"e":"composition-completed"})");
            Settle();
        }

        // The input method took the focus away itself: it is to be told
        // again next time, and it counts from nothing then. What was
        // half composed is dropped rather than left drawn.
        void FocusRemoved()
        {
            Say(R"({"e":"focus-removed"})");
            m_engagement.Removed();
            Forget();
        }

        // What the input method asks: the text, where it counts it to
        // be, and the caret.
        std::wstring TextRequested(int32_t start, int32_t end) const
        {
            return m_buffer.Text(start, end);
        }

        int32_t SelectionRequested() const noexcept { return m_buffer.Caret(); }

        bool Composing() const noexcept { return m_buffer.Composing(); }
        bool Active() const noexcept { return m_activation.Active(); }

    private:
        void Perform(Activation::Actions const& actions)
        {
            if (actions.checkLater)
            {
                m_effects.CheckLater();
            }
            if (actions.resumeLater)
            {
                m_effects.ResumeLater();
            }
            if (actions.leaveInputMethod)
            {
                Want(false);
            }
            if (actions.enterInputMethod)
            {
                Want(true);
            }
            if (actions.tellEmacsFocused || actions.tellEmacsUnfocused)
            {
                Say([&] {
                    return std::string{ R"({"out":"emacs-focus","focused":)" }
                           + Bool(actions.tellEmacsFocused) + "}";
                });
                m_effects.TellEmacsFocus(actions.tellEmacsFocused);
            }
        }

        void Want(bool engaged) { Tell(m_engagement.Want(engaged)); }

        void Tell(Engagement::Action action)
        {
            switch (action)
            {
            case Engagement::Action::Enter:
                // The input method counts from nothing again once told.
                Forget();
                Say(R"({"out":"enter"})");
                m_device.NotifyFocusEnter();
                break;
            case Engagement::Action::Leave:
                Say(R"({"out":"leave"})");
                m_device.NotifyFocusLeave();
                break;
            case Engagement::Action::None:
                break;
            }
        }

        // Hand on what the input method settled on. The input method is
        // told nothing: it counts on from where this ends, and so does
        // the buffer.
        void Settle()
        {
            auto settled = m_buffer.Settle();

            if (!settled.empty())
            {
                Say([&] { return R"({"out":"commit","text":)" + Quote(settled) + "}"; });
                m_effects.Commit(settled);
            }
            m_effects.Composing(std::wstring{});
        }

        void Forget()
        {
            bool drawn = m_buffer.Composing() || !m_buffer.Composed().empty();

            m_buffer.Reset();
            if (drawn)
            {
                m_effects.Composing(std::wstring{});
            }
        }

        // Write down what happened ("e"), or what was done about it
        // ("out", there to be read and not played back). Only while
        // something is listening: a line that is not written is not
        // made either, so that a keyboard nobody records costs nothing.
        void Say(char const* line)
        {
            if (m_recorder)
            {
                m_recorder(line);
            }
        }

        template <typename Make>
        void Say(Make&& make)
        {
            if (m_recorder)
            {
                m_recorder(make());
            }
        }

        static char const* Bool(bool value) noexcept { return value ? "true" : "false"; }

        static std::string Quote(std::wstring const& text)
        {
            std::string quoted = "\"";
            for (char c : text::ToUtf8(text))
            {
                switch (c)
                {
                case '"': quoted += "\\\""; break;
                case '\\': quoted += "\\\\"; break;
                case '\n': quoted += "\\n"; break;
                case '\r': quoted += "\\r"; break;
                case '\t': quoted += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char escape[8];
                        snprintf(escape, sizeof escape, "\\u%04x", c);
                        quoted += escape;
                    }
                    else
                    {
                        quoted += c;
                    }
                }
            }
            return quoted + "\"";
        }

        IKeyInputDevice& m_device;
        KeyboardEvents& m_effects;
        Recorder m_recorder;
        Activation m_activation;
        Engagement m_engagement;
        ImeBuffer m_buffer;
    };
}
