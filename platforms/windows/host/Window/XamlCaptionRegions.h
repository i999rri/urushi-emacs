#pragma once

#include "Window/CaptionRegions.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace urusi::window
{
    // The CaptionRegions of a window, found in its XAML and told to
    // Windows through the window's non-client pointer source.
    //
    // UI thread only.
    class XamlCaptionRegions : public INonClientRegions
    {
    public:
        explicit XamlCaptionRegions(winrt::Microsoft::UI::Windowing::AppWindow const& window);

        // Where TITLEBAR, the element Lisp named urusi-titlebar, is, and
        // where every control on it is, and not what is inside one: a
        // button is clicked as a whole. None while the window has a title
        // bar of its own.
        void Update(winrt::Microsoft::UI::Xaml::FrameworkElement const& titlebar);

        // INonClientRegions
        void Set(PixelRect caption, std::vector<PixelRect> const& passthrough) override;
        void Clear() override;

    private:
        winrt::Microsoft::UI::Windowing::AppWindow m_window{ nullptr };
        CaptionRegions m_regions{ *this };
    };
}
