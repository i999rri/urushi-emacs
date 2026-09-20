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

        winrt::Windows::UI::Text::Core::CoreTextEditContext m_context{ nullptr };
        CommitFn m_commit;
        ComposingFn m_composing_changed;

        // What the input method is turning over. It is the whole of
        // the text this context holds: Emacs keeps everything else.
        std::wstring m_text;
        bool m_composing{ false };
        winrt::Windows::Foundation::Rect m_caret{ 0, 0, 2, 16 };
    };
}
