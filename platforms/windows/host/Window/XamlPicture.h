#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Data.Json.h>

#include <string>

#include "Window/DrawCommand.h"

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
        void Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Put the picture where SITE is now, after it has been laid out
        // or moved.
        void Follow(winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Take the picture off the screen.  It is a bitmap and stays as
        // it was drawn, so one left where Emacs has gone would look
        // like an Emacs that is still there and answering nothing.
        void TakeAway();

        // Take in a "picture" message: the size of the whole screen,
        // the box of it that was drawn, and that box's pixels.  Return
        // why it could not be shown, or nothing if it was.
        std::string Show(winrt::Windows::Data::Json::JsonObject const& message);

        // Draw a screen Emacs said rather than drew.  Return why it
        // could not be drawn, or nothing if it was.
        std::string Draw(core::window::DrawFrame const& said);

    private:
        // Fill the box, as far as the clip allows.
        void FillBox(int x, int y, int width, int height, uint32_t color);
        void CopyBox(int x, int y, int width, int height, int toY);

        // The picture to draw into, of WIDTH by HEIGHT pixels, made
        // again when the screen is a different size.
        bool Ready(int width, int height);
        std::string Take(winrt::Windows::Data::Json::JsonObject const& drawn);
        void Move(winrt::Windows::Data::Json::JsonObject const& moved);

        winrt::Microsoft::UI::Xaml::Controls::Image m_image{ nullptr };
        // What holds the picture now, kept so that it can be taken
        // out again once that element is off the screen.
        winrt::Microsoft::UI::Xaml::Controls::Panel m_in{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        // How much room it was last given, to tell whether that changed.
        winrt::Windows::Foundation::Size m_room{ -1, -1 };
        int m_width{ 0 };
        int m_height{ 0 };

        // What drawing is kept within while a screen is being drawn,
        // which Emacs narrows to a row or to one run of text.
        bool m_clipped{ false };
        int m_clipX{ 0 };
        int m_clipY{ 0 };
        int m_clipWidth{ 0 };
        int m_clipHeight{ 0 };
    };
}
