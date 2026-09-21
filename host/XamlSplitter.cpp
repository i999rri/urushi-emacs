#include "pch.h"
#include "XamlSplitter.h"

#include <winrt/Microsoft.UI.Input.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace urusi
{
    std::shared_ptr<XamlSplitter> XamlSplitter::Attach(FrameworkElement const& element,
                                                       Splitter::Events events)
    {
        auto name = ParseSplitter(std::wstring{ element.Name() });
        if (!name)
        {
            return nullptr;
        }

        auto splitter = std::make_shared<XamlSplitter>(element, std::move(*name), std::move(events));
        splitter->Listen();
        return splitter;
    }

    XamlSplitter::XamlSplitter(FrameworkElement const& element, SplitterName name,
                               Splitter::Events events)
        : m_element(element), m_splitter(std::move(name), *this, *this, std::move(events))
    {
    }

    void XamlSplitter::Listen()
    {
        auto element = m_element.get();

        // The cursor says it can be dragged, and which way. It is a
        // protected property, meant for a control to set of itself, and
        // this one is set from outside.
        element.as<IUIElementProtected>().ProtectedCursor(
            Microsoft::UI::Input::InputSystemCursor::Create(
                m_splitter.Name().horizontal
                    ? Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast
                    : Microsoft::UI::Input::InputSystemCursorShape::SizeNorthSouth));

        auto self = shared_from_this();
        element.PointerPressed([self](winrt::Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(PointerKind::Pressed, args);
        });
        element.PointerMoved([self](winrt::Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(PointerKind::Moved, args);
        });
        element.PointerReleased([self](winrt::Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(PointerKind::Released, args);
        });
        element.PointerCaptureLost([self](winrt::Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            self->Pass(PointerKind::CaptureLost, args);
        });
    }

    // Tell the Splitter what the pointer did, counted from the corner of
    // the grid.
    void XamlSplitter::Pass(PointerKind kind, Input::PointerRoutedEventArgs const& args)
    {
        auto grid = Grid();
        if (!grid || Index() < 1)
        {
            return;
        }

        auto point = args.GetCurrentPoint(grid);
        auto position = point.Position();
        auto properties = point.Properties();

        PointerEvent event;
        event.kind = kind;
        event.x = position.X;
        event.y = position.Y;
        event.left = properties.IsLeftButtonPressed();
        event.button = kind == PointerKind::Pressed && event.left ? PointerButton::Left
                                                                   : PointerButton::None;
        if (kind == PointerKind::Released)
        {
            event.button = PointerButton::Left;
        }

        m_captured = args.Pointer();
        if (m_splitter.Handle(event))
        {
            args.Handled(true);
        }
    }

    void XamlSplitter::Capture()
    {
        if (auto element = m_element.get(); element && m_captured)
        {
            element.CapturePointer(m_captured);
        }
    }

    void XamlSplitter::Release()
    {
        if (auto element = m_element.get(); element && m_captured)
        {
            element.ReleasePointerCapture(m_captured);
        }
    }

    // The grid the splitter is in, and which cell of it is the
    // splitter's; the parts are in the cells either side.
    Controls::Grid XamlSplitter::Grid() const
    {
        auto element = m_element.get();
        return element ? element.Parent().try_as<Controls::Grid>() : nullptr;
    }

    int XamlSplitter::Index() const
    {
        auto element = m_element.get();
        if (!element)
        {
            return -1;
        }
        return m_splitter.Name().horizontal ? Controls::Grid::GetColumn(element)
                                            : Controls::Grid::GetRow(element);
    }

    double XamlSplitter::Length(bool before) const
    {
        auto grid = Grid();
        int index = Index() + (before ? -1 : 1);
        if (!grid || index < 0)
        {
            return 0;
        }
        return m_splitter.Name().horizontal ? grid.ColumnDefinitions().GetAt(index).ActualWidth()
                                            : grid.RowDefinitions().GetAt(index).ActualHeight();
    }

    bool XamlSplitter::Shares(bool before) const
    {
        auto grid = Grid();
        int index = Index() + (before ? -1 : 1);
        if (!grid || index < 0)
        {
            return false;
        }
        auto length = m_splitter.Name().horizontal ? grid.ColumnDefinitions().GetAt(index).Width()
                                                   : grid.RowDefinitions().GetAt(index).Height();
        return length.GridUnitType == GridUnitType::Star;
    }

    void XamlSplitter::SetLength(bool before, double length)
    {
        auto grid = Grid();
        int index = Index() + (before ? -1 : 1);
        if (!grid || index < 0)
        {
            return;
        }

        GridLength pixels{ length, GridUnitType::Pixel };
        if (m_splitter.Name().horizontal)
        {
            grid.ColumnDefinitions().GetAt(index).Width(pixels);
        }
        else
        {
            grid.RowDefinitions().GetAt(index).Height(pixels);
        }
    }
}
