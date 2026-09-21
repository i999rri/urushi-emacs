#include "pch.h"
#include "XamlCaptionRegions.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <functional>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace urusi
{
    XamlCaptionRegions::XamlCaptionRegions(Microsoft::UI::Windowing::AppWindow const& window)
        : m_window(window)
    {
    }

    void XamlCaptionRegions::Update(FrameworkElement const& titlebar)
    {
        auto overlapped = m_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!titlebar || !titlebar.XamlRoot() || (overlapped && overlapped.HasTitleBar()))
        {
            m_regions.Update(std::nullopt, {}, 1);
            return;
        }

        // Windows counts from the corner of the window's client area,
        // which is where XAML's root is.
        auto area = [](FrameworkElement const& element) {
            auto corner = element.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
            return Area{ corner.X, corner.Y, element.ActualWidth(), element.ActualHeight() };
        };

        std::vector<Area> controls;
        std::function<void(DependencyObject const&)> collect = [&](DependencyObject const& parent) {
            int count = Media::VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = Media::VisualTreeHelper::GetChild(parent, i);
                auto control = child.try_as<Controls::Control>();
                if (control && control.IsHitTestVisible() && control.Visibility() == Visibility::Visible)
                {
                    controls.push_back(area(control));
                }
                else
                {
                    collect(child);
                }
            }
        };
        collect(titlebar);

        m_regions.Update(area(titlebar), controls, titlebar.XamlRoot().RasterizationScale());
    }

    void XamlCaptionRegions::Set(PixelRect caption, std::vector<PixelRect> const& passthrough)
    {
        using Microsoft::UI::Input::NonClientRegionKind;

        auto rect = [](PixelRect const& r) {
            return winrt::Windows::Graphics::RectInt32{ r.x, r.y, r.width, r.height };
        };

        std::vector<winrt::Windows::Graphics::RectInt32> controls;
        controls.reserve(passthrough.size());
        for (auto const& r : passthrough)
        {
            controls.push_back(rect(r));
        }

        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(m_window.Id());
        source.SetRegionRects(NonClientRegionKind::Caption, { rect(caption) });
        source.SetRegionRects(NonClientRegionKind::Passthrough, controls);
    }

    void XamlCaptionRegions::Clear()
    {
        using Microsoft::UI::Input::NonClientRegionKind;

        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(m_window.Id());
        source.ClearRegionRects(NonClientRegionKind::Caption);
        source.ClearRegionRects(NonClientRegionKind::Passthrough);
    }
}
