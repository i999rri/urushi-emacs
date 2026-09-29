#pragma once

#include "Emacs/Emacs.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Data.Json.h>

#include <memory>

namespace urushi::windows::window
{
    // The screen Lisp builds, in a XAML panel of the window: the XAML
    // around the rows, put in whole when it has changed, and the rows
    // of each panel in it, the ones that changed built and the rest
    // kept. The events Lisp asked for on its elements go to Emacs.
    //
    // UI thread only.
    class XamlScreen
    {
    public:
        XamlScreen(winrt::Microsoft::UI::Xaml::Controls::Panel const& surface,
                   std::shared_ptr<emacs::Emacs> emacs);

        // Put in the XAML around the rows, if MESSAGE, a screen, has
        // it. Returns whether it did: everything Lisp named in it is a
        // new element then.
        bool ShowChrome(winrt::Windows::Data::Json::JsonObject const& message);

        // Put the rows of MESSAGE in the panels they are for.
        void ShowRows(winrt::Windows::Data::Json::JsonObject const& message);

        // The element of the XAML around the rows that Lisp named NAME,
        // or null.
        winrt::Microsoft::UI::Xaml::FrameworkElement Named(winrt::hstring const& name) const;

        // Say how wide a character of the font MESSAGE asks about is.
        // Emacs lays its text out on a grid of whole pixels and XAML
        // does not, so the two drift apart across a line unless Emacs
        // is told what to correct for.
        void Measure(winrt::Windows::Data::Json::JsonObject const& message) const;

    private:
        winrt::Microsoft::UI::Xaml::FrameworkElement Root() const;
        winrt::Microsoft::UI::Xaml::Controls::Panel FindPanel(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& root, winrt::hstring const& name) const;
        void ReconcileRows(winrt::Microsoft::UI::Xaml::Controls::Panel const& panel,
                           winrt::Windows::Data::Json::JsonArray const& items);
        void AttachEvents(winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
                          winrt::Windows::Data::Json::JsonArray const& events) const;

        winrt::Microsoft::UI::Xaml::Controls::Panel m_surface;
        std::shared_ptr<emacs::Emacs> m_emacs;
    };
}
