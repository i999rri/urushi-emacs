#pragma once

#include "Window/FrameView.h"
#include "Input/MouseTranslation.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <functional>
#include <memory>

namespace urushi::windows::window
{
    // A FrameView on a XAML element: what the pointer does over the
    // element goes to the view, and the view's frame is reached through
    // what the window gives it.
    //
    // UI thread only. The element's handlers keep this alive, and it
    // holds the element only weakly, so that the two go together.
    class XamlFrameView : public core::input::IPointerInputDevice, public core::window::IFrameWindow,
                          public std::enable_shared_from_this<XamlFrameView>
    {
    public:
        struct Frame
        {
            // The window of the frame, which may come to be only after
            // the element does: the frame the window shows is made as
            // Emacs starts.
            std::function<HWND()> window;

            // Tell the frame ID its size.
            std::function<void(std::wstring const& id, core::window::PixelSize size)> tellSize;

            // Give the frame ID what the pointer did, for an Emacs that
            // takes the pointer as messages and has no window to post it
            // to. Without it, the pointer is posted to the window.
            std::function<void(std::wstring const& id, core::input::PointerEvent const& event,
                               double scale)> sendPointer;
        };

        // A view of the frame ID in ELEMENT.
        static std::shared_ptr<XamlFrameView> Attach(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& element, std::wstring id,
            Frame frame, core::window::FrameSizes& sizes);

        XamlFrameView(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                      std::wstring id, Frame frame, core::window::FrameSizes& sizes);

        std::wstring const& Id() const noexcept { return m_view.Id(); }

        // Tell the frame how big the element is now, if that changed.
        void Resize();

        // IPointerInputDevice
        void Capture() override;
        void Release() override;

        // IFrameWindow
        void PostPointer(core::input::PointerEvent const& event, double scale) override;
        void TellSize(core::window::PixelSize size) override;

    private:
        void Listen();
        void Pass(core::input::PointerKind kind,
                  winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

        winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> m_element;
        winrt::Microsoft::UI::Xaml::Input::Pointer m_pointer{ nullptr };
        Frame m_frame;
        core::window::FrameView m_view;
    };
}
