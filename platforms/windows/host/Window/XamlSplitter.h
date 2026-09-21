#pragma once

#include "Window/Splitter.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <memory>

namespace urusi::window
{
    // A Splitter on a XAML element: what the pointer does over the
    // element goes to the Splitter, and the Splitter's parts are the
    // columns or rows either side of the element's in the Grid it is in.
    //
    // UI thread only. The element's handlers keep this alive, and it
    // holds the element only weakly, so that the two go together.
    class XamlSplitter : public input::IPointerInputDevice, public ISplitterTracks,
                         public std::enable_shared_from_this<XamlSplitter>
    {
    public:
        // A splitter for ELEMENT, if it is named as one, told EVENTS.
        static std::shared_ptr<XamlSplitter> Attach(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
            Splitter::Events events);

        XamlSplitter(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                     SplitterName name, Splitter::Events events);

        Splitter const& Model() const noexcept { return m_splitter; }

        // IPointerInputDevice
        void Capture() override;
        void Release() override;

        // ISplitterTracks
        double Length(bool before) const override;
        bool Shares(bool before) const override;
        void SetLength(bool before, double length) override;

    private:
        void Listen();
        void Pass(input::PointerKind kind,
                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        winrt::Microsoft::UI::Xaml::Controls::Grid Grid() const;
        int Index() const;

        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_element;
        winrt::Microsoft::UI::Xaml::Input::Pointer m_captured{ nullptr };
        Splitter m_splitter;
    };
}
