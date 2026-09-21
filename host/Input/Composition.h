#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.Text.Core.h>

#include "Input/Keyboard.h"

namespace urusi::input
{
    // The input method's side of the window.
    //
    // An input method does not send characters; it holds a
    // conversation. It asks what the text around the caret is, offers
    // a composition that changes as the user picks through it, and
    // only at the end says what was settled on. Windows holds that
    // conversation through a CoreTextEditContext, and this is one.
    //
    // It is the device the keys come from, to the Keyboard: what to make
    // of the conversation is the Keyboard's to decide, and this passes on
    // what the context says, answers what it asks from the Keyboard, and
    // tells the context what the Keyboard asks.
    //
    // UI thread only.
    class Composition : public IKeyInputDevice
    {
    public:
        // ELEMENT is what holds the focus while typing; the context is
        // made for the window it is in. KEYBOARD hears everything the
        // context says, and outlives this.
        void Start(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                   Keyboard& keyboard);

        // IKeyInputDevice
        void NotifyFocusEnter() override;
        void NotifyFocusLeave() override;

        // Where the caret is on the screen, which is where the
        // candidates are shown.
        void SetCaret(winrt::Windows::Foundation::Rect const& caret);

    private:
        void Bind();

        winrt::Windows::UI::Text::Core::CoreTextEditContext m_context{ nullptr };
        Keyboard* m_keyboard{ nullptr };
        winrt::Windows::Foundation::Rect m_caret{ 0, 0, 2, 16 };
    };
}
