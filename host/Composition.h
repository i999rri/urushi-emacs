#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.Text.Core.h>

#include "Session.h"

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
    // What to make of the conversation is the Session's to decide: this
    // passes on what the context says, answers what it asks from the
    // Session, and does to the context what the Session asks.
    //
    // UI thread only.
    class Composition
    {
    public:
        // ELEMENT is what holds the focus while typing; the context is
        // made for the window it is in. SESSION hears everything the
        // context says, and outlives this.
        void Start(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                   Session& session);

        // Tell the context the focus has come, or gone.
        void NotifyFocusEnter();
        void NotifyFocusLeave();

        // Where the caret is on the screen, which is where the
        // candidates are shown.
        void SetCaret(winrt::Windows::Foundation::Rect const& caret);

    private:
        void Bind();

        winrt::Windows::UI::Text::Core::CoreTextEditContext m_context{ nullptr };
        Session* m_session{ nullptr };
        winrt::Windows::Foundation::Rect m_caret{ 0, 0, 2, 16 };
    };
}
