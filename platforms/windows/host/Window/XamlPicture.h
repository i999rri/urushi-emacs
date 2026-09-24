#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Data.Json.h>

#include <string>

namespace urusi::windows::window
{
    // The screen as Emacs drew it.
    //
    // Emacs reads the font files and rasterizes the glyphs itself, so
    // it lays the text out and draws it by the same measurements, and
    // what arrives here is pixels rather than an account of them.
    // Only the part drawn into since the last one comes each time.
    //
    // Of the user interface thread, like everything else that touches
    // an element.
    class XamlPicture
    {
    public:
        // Put the picture in SITE, the element Lisp said the frame is
        // drawn in, and take it out of wherever it was before.  Where
        // that element sits is Lisp's to lay out, so the frame Emacs
        // drew lands where Lisp meant the frame to be, with the rest
        // of the window left to the chrome around it.
        void Attach(winrt::Microsoft::UI::Xaml::Controls::Panel const& surface,
                    winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Put the picture where SITE is now, after it has been laid out
        // or moved.
        void Follow(winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Take in a "picture" message: the size of the whole screen,
        // the box of it that was drawn, and that box's pixels.  Return
        // why it could not be shown, or nothing if it was.
        std::string Show(winrt::Windows::Data::Json::JsonObject const& message);

    private:
        // The picture to draw into, of WIDTH by HEIGHT pixels, made
        // again when the screen is a different size.
        bool Ready(int width, int height);

        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Image m_image{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        // Where the picture was last laid and how much room it was
        // given, to tell whether either changed.
        float m_left{ -1 };
        float m_top{ -1 };
        winrt::Windows::Foundation::Size m_room{ -1, -1 };
        int m_width{ 0 };
        int m_height{ 0 };
    };
}
