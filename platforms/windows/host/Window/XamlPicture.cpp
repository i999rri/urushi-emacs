#include "pch.h"

#include "XamlPicture.h"

#include <robuffer.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <string>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
// In full: robuffer.h puts the interface that reaches a buffer's bytes
// in a `Windows' namespace of its own, beside the projected one.
using namespace winrt::Windows::Data::Json;

namespace
{
    // The bytes a buffer holds, which is how a picture is handed over
    // and how one is written into.
    uint8_t* BytesOf(winrt::Windows::Storage::Streams::IBuffer const& buffer)
    {
        auto access = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
        uint8_t* bytes{ nullptr };

        check_hresult(access->Buffer(&bytes));
        return bytes;
    }
}

namespace urusi::windows::window
{
    void XamlPicture::Attach(Controls::Panel const& surface, FrameworkElement const& site)
    {
        if (!surface || !site)
        {
            return;
        }

        if (!m_image)
        {
            m_image = Controls::Image();
            // Drawn a pixel of the picture to a pixel of the window,
            // from its top left corner: Emacs drew it for the size the
            // element was said to be, and stretching it would be
            // showing something other than what it drew.
            m_image.Stretch(Media::Stretch::None);
            m_image.IsHitTestVisible(false);
            if (m_bitmap)
            {
                m_image.Source(m_bitmap);
            }

            // In a Canvas, which asks for no room of its own however
            // large what is in it, so that the picture cannot widen
            // what it is laid over.
            m_canvas = Controls::Canvas();
            m_canvas.IsHitTestVisible(false);
            m_canvas.Children().Append(m_image);
        }

        // Over the screen Lisp built rather than within the element
        // Lisp said the frame is drawn in: that element's size is what
        // Emacs lays the text out to, and a picture inside it would be
        // making the frame it was drawn for bigger.  It is moved to
        // where that element is instead.
        if (!m_canvas.Parent())
        {
            surface.Children().Append(m_canvas);
        }
        Follow(site);
    }

    // Put the picture where SITE is, which is where Lisp laid out the
    // frame Emacs drew.
    void XamlPicture::Follow(FrameworkElement const& site)
    {
        if (!m_canvas || !site.XamlRoot())
        {
            return;
        }

        auto corner = site.TransformToVisual(m_canvas.Parent().try_as<UIElement>())
                          .TransformPoint({ 0, 0 });

        auto room = winrt::Windows::Foundation::Size{ static_cast<float>(site.ActualWidth()),
                                               static_cast<float>(site.ActualHeight()) };

        // Only where it moved or the room changed: this is said after
        // every layout, and laying the picture out again would call for
        // another one.
        if (corner.X == m_left && corner.Y == m_top
            && room.Width == m_room.Width && room.Height == m_room.Height)
        {
            return;
        }

        m_left = corner.X;
        m_top = corner.Y;
        m_room = room;
        m_canvas.Margin(Thickness{ corner.X, corner.Y, 0, 0 });
        m_canvas.HorizontalAlignment(HorizontalAlignment::Left);
        m_canvas.VerticalAlignment(VerticalAlignment::Top);

        // Cut to the room it was given: the picture is the whole frame,
        // and Emacs is given a frame taller than the room where Lisp
        // means to draw part of it itself, as it does with the echo
        // area when the status bar is to say what it says.
        Media::RectangleGeometry clip;
        clip.Rect(winrt::Windows::Foundation::Rect{ 0, 0, room.Width, room.Height });
        m_canvas.Clip(clip);
    }

    bool XamlPicture::Ready(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        if (m_bitmap && m_width == width && m_height == height)
        {
            return true;
        }

        m_bitmap = Media::Imaging::WriteableBitmap(width, height);
        m_width = width;
        m_height = height;
        if (m_image)
        {
            m_image.Source(m_bitmap);
        }
        return true;
    }

    std::string XamlPicture::Show(JsonObject const& message)
    {
        auto drawn = message.GetNamedObject(L"drawn", nullptr);
        if (!drawn)
        {
            return "no drawn box";
        }

        if (!Ready(static_cast<int>(message.GetNamedNumber(L"width", 0)),
                   static_cast<int>(message.GetNamedNumber(L"height", 0))))
        {
            return "no picture that size";
        }

        int const x = static_cast<int>(drawn.GetNamedNumber(L"x", 0));
        int const y = static_cast<int>(drawn.GetNamedNumber(L"y", 0));
        int const width = static_cast<int>(drawn.GetNamedNumber(L"width", 0));
        int const height = static_cast<int>(drawn.GetNamedNumber(L"height", 0));

        // The box has to lie inside the picture: it is written into
        // without asking again, and Emacs may have been drawing for a
        // size the picture is no longer.
        if (width <= 0 || height <= 0 || x < 0 || y < 0
            || x + width > m_width || y + height > m_height)
        {
            return "box " + std::to_string(x) + "," + std::to_string(y) + " "
                + std::to_string(width) + "x" + std::to_string(height) + " outside "
                + std::to_string(m_width) + "x" + std::to_string(m_height);
        }

        auto cells = winrt::Windows::Security::Cryptography::CryptographicBuffer::
            DecodeFromBase64String(message.GetNamedString(L"cells", L""));
        constexpr uint32_t kPixel = 4;
        if (cells.Length() != static_cast<uint32_t>(width) * height * kPixel)
        {
            return "got " + std::to_string(cells.Length()) + " bytes, wanted "
                + std::to_string(static_cast<uint32_t>(width) * height * kPixel);
        }

        uint8_t const* from = BytesOf(cells);
        uint8_t* into = BytesOf(m_bitmap.PixelBuffer());

        for (int row = 0; row < height; row++)
        {
            std::copy_n(from + static_cast<size_t>(row) * width * kPixel,
                        static_cast<size_t>(width) * kPixel,
                        into + (static_cast<size_t>(y + row) * m_width + x) * kPixel);
        }

        m_bitmap.Invalidate();
        return {};
    }
}
