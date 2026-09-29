#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <memory>
#include <string>

#include "Window/DrawCommand.h"
#include "Window/XamlFonts.h"
#include "Window/XamlImages.h"

namespace urushi::windows::window
{
    // The screen of an Emacs frame, drawn from what Emacs said to draw.
    //
    // Emacs says what it drew rather than handing over the pixels, so
    // the drawing happens here, with Direct2D: the areas it filled are
    // rectangles and its text is a run of glyphs, both of which the
    // graphics card draws.  Nothing walks the pixels of the screen,
    // which is what handing them over came to.
    //
    // What was drawn is kept in a bitmap of its own rather than in the
    // swap chain: Emacs draws only what changed, so the rest of the
    // screen has to still be there, and a swap chain gives back a
    // different buffer each time.
    //
    // Of the user interface thread, like everything else that touches
    // an element.
    class XamlDrawing
    {
    public:
        XamlDrawing(std::shared_ptr<XamlFonts> fonts,
                    std::shared_ptr<XamlImages> images)
            : m_fonts(std::move(fonts)), m_images(std::move(images))
        {
        }

        // Put the screen in SITE, the element Lisp said the frame is
        // drawn in, and take it out of wherever it was before.
        void Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Put it where SITE is now, after it has been laid out.
        void Follow(winrt::Microsoft::UI::Xaml::FrameworkElement const& site);

        // Take it off the screen.
        void TakeAway();

        // Show what was last drawn again, without drawing it: the
        // chrome around the screen is built again whenever it changes,
        // and the screen has to find its way back onto it.
        void Again();

        // Draw what SAID says, and show it.  Returns why it could not
        // be drawn, or nothing.
        std::string Draw(core::window::DrawFrame const& said);

    private:
        // The device, the chain and the bitmap drawn into, made again
        // when the screen is a different size.  Returns false where
        // there is no drawing to be had at all.
        bool Ready(int width, int height);
        bool Device();
        void Show();

        void Fill(core::window::DrawCommand const& command);
        void Glyphs(core::window::DrawCommand const& command);
        void Image(core::window::DrawCommand const& command);
        void Copy(core::window::DrawCommand const& command);
        void Clip(core::window::DrawCommand const& command);
        void Unclip();
        winrt::com_ptr<ID2D1SolidColorBrush> Brush(uint32_t color);

        std::shared_ptr<XamlFonts> m_fonts;
        std::shared_ptr<XamlImages> m_images;

        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel m_panel{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Panel m_in{ nullptr };
        winrt::Windows::Foundation::Size m_room{ -1, -1 };

        winrt::com_ptr<ID3D11Device> m_d3d;
        winrt::com_ptr<ID2D1Device6> m_device;
        winrt::com_ptr<ID2D1DeviceContext5> m_context;
        winrt::com_ptr<IDXGISwapChain1> m_chain;
        // What has been drawn so far, which the next screen draws on.
        winrt::com_ptr<ID2D1Bitmap1> m_canvas;
        // Room to move a piece of the canvas through: Direct2D reads
        // and writes different bitmaps, not one.
        winrt::com_ptr<ID2D1Bitmap1> m_spare;
        winrt::com_ptr<ID2D1SolidColorBrush> m_brush;

        int m_width{ 0 };
        int m_height{ 0 };
        bool m_clipped{ false };
        bool m_drawing{ false };
    };
}
