#include "pch.h"

#include "XamlPicture.h"

#include <robuffer.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cstring>
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
    void XamlPicture::Attach(FrameworkElement const& site)
    {
        auto room = site.try_as<Controls::Panel>();

        if (!room)
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
        }

        // Within the element Lisp named for it, so that whatever Lisp
        // put around the frame is around the picture as well: a border
        // with rounded corners cuts the picture to them, and a shadow
        // falls outside it.  The element is a Canvas, which asks for no
        // room of its own however large what is in it, so the picture
        // cannot widen the frame it was drawn for.
        // Taken out of where it was by the element that holds it
        // rather than by asking the picture what holds it: an element
        // taken off the screen says it has no parent while still
        // holding what is in it, and putting that somewhere else
        // without taking it out first is refused.
        if (m_in)
        {
            if (m_in == room)
            {
                Follow(site);
                return;
            }

            uint32_t at{};
            if (m_in.Children().IndexOf(m_image, at))
            {
                m_in.Children().RemoveAt(at);
            }
            m_in = nullptr;
        }

        // First, so that anything Lisp draws over the frame -- a child
        // frame floating on it -- is drawn over the picture.
        room.Children().InsertAt(0, m_image);
        m_in = room;
        Follow(site);
    }

    void XamlPicture::TakeAway()
    {
        uint32_t at{};

        if (m_in && m_image && m_in.Children().IndexOf(m_image, at))
        {
            m_in.Children().RemoveAt(at);
        }
        m_in = nullptr;
    }

    // Cut the picture to the room SITE was given.  A Canvas draws what
    // is in it however far past itself that reaches, and the picture is
    // the whole frame, which is taller than its room wherever Lisp
    // means to draw part of it itself: the echo area is left out of
    // sight that way when a status bar is to say what it says.
    void XamlPicture::Follow(FrameworkElement const& site)
    {
        auto room = winrt::Windows::Foundation::Size{
            static_cast<float>(site.ActualWidth()),
            static_cast<float>(site.ActualHeight())
        };

        // Only where the room changed: this is said after every layout
        // of the element, and cutting the picture again would call for
        // another one.
        if (!m_image || (room.Width == m_room.Width && room.Height == m_room.Height))
        {
            return;
        }

        m_room = room;

        Media::RectangleGeometry clip;
        clip.Rect(winrt::Windows::Foundation::Rect{ 0, 0, room.Width, room.Height });
        m_image.Clip(clip);
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

    // Move one box of the picture, which is what a window scrolling
    // comes to: what it holds is here already.
    void XamlPicture::Move(JsonObject const& moved)
    {
        int const x = static_cast<int>(moved.GetNamedNumber(L"x", 0));
        int const y = static_cast<int>(moved.GetNamedNumber(L"y", 0));
        int const width = static_cast<int>(moved.GetNamedNumber(L"width", 0));
        int const height = static_cast<int>(moved.GetNamedNumber(L"height", 0));
        int const toY = static_cast<int>(moved.GetNamedNumber(L"toY", 0));
        constexpr uint32_t kPixel = 4;

        if (width <= 0 || height <= 0 || x < 0 || y < 0 || toY < 0
            || x + width > m_width
            || y + height > m_height || toY + height > m_height)
        {
            return;
        }

        uint8_t* cells = BytesOf(m_bitmap.PixelBuffer());
        size_t const stride = static_cast<size_t>(m_width) * kPixel;
        size_t const run = static_cast<size_t>(width) * kPixel;

        // From the end the rows are moving towards, so that a box moved
        // onto itself is not read after it has been written.
        for (int row = 0; row < height; row++)
        {
            int const at = toY < y ? row : height - 1 - row;

            std::memmove(cells + (toY + at) * stride + x * kPixel,
                         cells + (y + at) * stride + x * kPixel, run);
        }
    }

    // One box of a picture: where it is and the pixels of it.  Return
    // why it could not be taken, or nothing if it was.
    std::string XamlPicture::Take(JsonObject const& drawn)
    {
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
            DecodeFromBase64String(drawn.GetNamedString(L"cells", L""));
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
        return {};
    }

    std::string XamlPicture::Show(JsonObject const& message)
    {
        auto drawn = message.GetNamedArray(L"drawn", nullptr);
        if (!drawn)
        {
            return "no drawn boxes";
        }

        if (!Ready(static_cast<int>(message.GetNamedNumber(L"width", 0)),
                   static_cast<int>(message.GetNamedNumber(L"height", 0))))
        {
            return "no picture that size";
        }

        // What moved moves first: the boxes drawn are what was drawn
        // after the moving, and are to go over it.
        for (auto const& one : message.GetNamedArray(L"moved", JsonArray{}))
        {
            Move(one.GetObject());
        }

        // Several of them: a keystroke draws the line being typed in
        // and the mode line below it, and one box around both is the
        // whole frame between them.
        for (auto const& one : drawn)
        {
            if (auto why = Take(one.GetObject()); !why.empty())
            {
                return why;
            }
        }

        m_bitmap.Invalidate();
        return {};
    }
}
