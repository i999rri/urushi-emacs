#pragma once

#include <winrt/Windows.Data.Json.h>

#include <d2d1_3.h>
#include <winrt/base.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace urusi::windows::window
{
    // The images Emacs draws, as this window has them.
    //
    // Emacs decodes an image itself, whatever format it was in, and
    // says to draw the number it gave it rather than sending the pixels
    // again for every screen that shows it.  So a screen may name an
    // image this window has never seen; the pixels are asked for once,
    // and until they arrive that image is not drawn.
    //
    // What arrives is what is drawn: four bytes to a pixel, blue first,
    // already multiplied by the alpha that Emacs's mask decided.  Emacs
    // is the one that knows what a mask means, so nothing here decides
    // anything about the pixels.
    //
    // The pixels arrive on the thread that reads from Emacs and the
    // bitmap is made where the window is drawn, so the pixels are
    // locked and the bitmaps are not: only the drawing thread touches
    // those.
    class XamlImages
    {
    public:
        // Ask Emacs for the pixels of the image it knows by this
        // number.  Called where an image is to be drawn and its pixels
        // are not here.
        void OnWanting(std::function<void(int)> ask);

        // Take an "image" message: the pixels of one, as Emacs draws
        // them.  Returns what went wrong, or nothing.
        std::string Take(winrt::Windows::Data::Json::JsonObject const& message);

        // Forget the image Emacs has let go of, whose number it may
        // give to another image later.
        void Gone(int id);

        // The bitmap of ID for CONTEXT, or null if the pixels are not
        // here yet, in which case they are asked for.
        winrt::com_ptr<ID2D1Bitmap1> Bitmap(ID2D1DeviceContext5* context,
                                            int id);

        // Let go of every bitmap, the device they were made for having
        // gone.  The pixels are kept, so nothing is asked for twice.
        void Forget();

    private:
        struct Image
        {
            int width{};
            int height{};
            std::vector<uint8_t> pixels;
            bool asked{};
            // Made from the pixels on first use, and again after a
            // device is lost.
            winrt::com_ptr<ID2D1Bitmap1> bitmap;
        };

        mutable std::mutex m_lock;
        std::function<void(int)> m_ask;
        std::map<int, Image> m_images;
    };
}
