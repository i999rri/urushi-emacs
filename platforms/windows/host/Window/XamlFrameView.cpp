#include "pch.h"
#include "Window/XamlFrameView.h"

#include <winrt/Microsoft.UI.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace urusi::windows::window
{
    std::shared_ptr<XamlFrameView> XamlFrameView::Attach(FrameworkElement const& element,
                                                         std::wstring id, Frame frame,
                                                         core::window::FrameSizes& sizes)
    {
        auto view = std::make_shared<XamlFrameView>(element, std::move(id), std::move(frame),
                                                     sizes);
        view->Listen();
        return view;
    }

    XamlFrameView::XamlFrameView(FrameworkElement const& element, std::wstring id, Frame frame,
                                 core::window::FrameSizes& sizes)
        : m_element(element), m_frame(std::move(frame)),
          m_view(std::move(id), *this, *this, sizes)
    {
    }

    void XamlFrameView::Listen()
    {
        using winrt::Windows::Foundation::IInspectable;

        auto element = m_element.get();
        auto self = shared_from_this();
        element.PointerPressed([self](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(core::input::PointerKind::Pressed, args);
        });
        element.PointerReleased([self](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(core::input::PointerKind::Released, args);
        });
        element.PointerMoved([self](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(core::input::PointerKind::Moved, args);
        });
        element.PointerWheelChanged([self](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(core::input::PointerKind::Wheel, args);
        });
    }

    // Tell the view what the pointer did, counted from the corner of the
    // element.
    void XamlFrameView::Pass(core::input::PointerKind kind, Input::PointerRoutedEventArgs const& args)
    {
        using Microsoft::UI::Input::PointerUpdateKind;
        using winrt::Windows::System::VirtualKeyModifiers;

        auto element = m_element.get();
        if (!element)
        {
            return;
        }

        auto point = args.GetCurrentPoint(element);
        auto properties = point.Properties();
        auto modifiers = args.KeyModifiers();

        core::input::PointerEvent event;
        event.kind = kind;
        event.x = point.Position().X;
        event.y = point.Position().Y;
        event.left = properties.IsLeftButtonPressed();
        event.right = properties.IsRightButtonPressed();
        event.middle = properties.IsMiddleButtonPressed();
        event.shift = (modifiers & VirtualKeyModifiers::Shift) != VirtualKeyModifiers::None;
        event.control = (modifiers & VirtualKeyModifiers::Control) != VirtualKeyModifiers::None;
        event.wheel = properties.MouseWheelDelta();
        event.horizontal = properties.IsHorizontalMouseWheel();
        switch (properties.PointerUpdateKind())
        {
        case PointerUpdateKind::LeftButtonPressed:
        case PointerUpdateKind::LeftButtonReleased:
            event.button = core::input::PointerButton::Left;
            break;
        case PointerUpdateKind::RightButtonPressed:
        case PointerUpdateKind::RightButtonReleased:
            event.button = core::input::PointerButton::Right;
            break;
        case PointerUpdateKind::MiddleButtonPressed:
        case PointerUpdateKind::MiddleButtonReleased:
            event.button = core::input::PointerButton::Middle;
            break;
        default:
            break;
        }

        double scale = element.XamlRoot() ? element.XamlRoot().RasterizationScale() : 1.0;
        m_pointer = args.Pointer();
        if (m_view.Pointer(event, scale))
        {
            args.Handled(true);
        }
    }

    void XamlFrameView::Resize()
    {
        auto element = m_element.get();
        if (!element || !element.XamlRoot())
        {
            return;
        }

        // XAML works in device-independent pixels and Emacs in the ones
        // of the screen.
        m_view.Resized(element.ActualWidth(), element.ActualHeight(),
                       element.XamlRoot().RasterizationScale());
    }

    void XamlFrameView::Capture()
    {
        if (auto element = m_element.get(); element && m_pointer)
        {
            element.CapturePointer(m_pointer);
        }
    }

    void XamlFrameView::Release()
    {
        if (auto element = m_element.get(); element && m_pointer)
        {
            element.ReleasePointerCapture(m_pointer);
        }
    }

    // The frame's window is posted the message Windows would have sent it
    // for the mouse.
    void XamlFrameView::PostPointer(core::input::PointerEvent const& event, double scale)
    {
        HWND window = m_frame.window ? m_frame.window() : nullptr;
        auto message = input::TranslatePointer(event, scale);
        if (window && message)
        {
            PostMessageW(window, message->message, message->wParam, message->lParam);
        }
    }

    void XamlFrameView::TellSize(core::window::PixelSize size)
    {
        if (m_frame.tellSize)
        {
            m_frame.tellSize(m_view.Id(), size);
        }
    }
}
