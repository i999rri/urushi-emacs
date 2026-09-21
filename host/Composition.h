#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.Text.Core.h>

#include <functional>
#include <string>

namespace urusi
{
    // The input method's side of the window.
    //
    // An input method does not send characters; it holds a
    // conversation. It asks what the text around the caret is, offers
    // a composition that changes as the user picks through it, and
    // only at the end says what was settled on. Windows holds that
    // conversation through a CoreTextEditContext, and this is one.
    //
    // Emacs is on the other side of it and knows nothing of that
    // conversation: it is told what was settled on, as the characters
    // it would have received had they been typed. What is still being
    // composed is passed on to be drawn, because the window that would
    // otherwise draw it cannot be seen.
    //
    // UI thread only.
    class Composition
    {
    public:
        // Called with what the input method has settled on, and with
        // what it is still turning over, which is empty once there is
        // nothing.
        using CommitFn = std::function<void(std::wstring)>;
        using ComposingFn = std::function<void(std::wstring)>;

        // Where to write what the conversation was, for as long as
        // anyone is likely to read it.
        using TraceFn = std::function<void(std::string)>;
        void Trace(TraceFn fn) { m_trace = std::move(fn); }

        // ELEMENT is what holds the focus while typing; the context is
        // made for the window it is in.
        void Start(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                   CommitFn commit, ComposingFn composing);

        // Take the input, or give it back.
        void Focus(bool enter);

        // Where the caret is on the screen, which is where the
        // candidates are shown.
        void SetCaret(winrt::Windows::Foundation::Rect const& caret);

        // Whether a composition is under way, so that keys belonging to
        // it are not passed on as keys.
        bool Composing() const noexcept { return m_composing; }

    private:
        void Bind();
        void Settle();
        void Reset();
        void Say(std::string const& what);

        winrt::Windows::UI::Text::Core::CoreTextEditContext m_context{ nullptr };
        CommitFn m_commit;
        ComposingFn m_composing_changed;
        TraceFn m_trace;

        // Enough of the conversation to see its shape, and not so much
        // that a day's typing fills a disk.
        int m_said{ 0 };

        // What the input method is turning over. It is the whole of
        // the text this context holds: Emacs keeps everything else.
        std::wstring m_text;
        bool m_composing{ false };

        // Whether the context has been told the focus is here.
        bool m_entered{ false };
        winrt::Windows::Foundation::Rect m_caret{ 0, 0, 2, 16 };
    };
}
