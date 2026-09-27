#include "pch.h"

#include "XamlImages.h"

#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;

namespace urusi::windows::window
{
    void XamlImages::OnWanting(std::function<void(int)> ask)
    {
        std::lock_guard hold{ m_lock };

        m_ask = std::move(ask);
    }

    std::string XamlImages::Take(Windows::Data::Json::JsonObject const& message)
    {
        auto const id = static_cast<int>(message.GetNamedNumber(L"id", -1));
        auto const width = static_cast<int>(message.GetNamedNumber(L"width", 0));
        auto const height = static_cast<int>(message.GetNamedNumber(L"height", 0));

        if (id < 0 || width <= 0 || height <= 0)
        {
            return "an image of no size";
        }

        auto const pixels = Windows::Security::Cryptography::CryptographicBuffer::
            DecodeFromBase64String(message.GetNamedString(L"pixels", L""));
        auto const room = static_cast<size_t>(width) * height * 4;

        // Four bytes to a pixel and no other count will do: a bitmap
        // made from fewer would be read past its end.
        if (pixels.Length() != room)
        {
            return "image " + std::to_string(id) + " came as "
                   + std::to_string(pixels.Length()) + " bytes, not "
                   + std::to_string(room);
        }

        std::lock_guard hold{ m_lock };
        auto& image = m_images[id];

        image.width = width;
        image.height = height;
        image.pixels.assign(pixels.data(), pixels.data() + pixels.Length());
        // Made where the drawing is, from these.
        image.bitmap = nullptr;
        return {};
    }

    void XamlImages::Gone(int id)
    {
        std::lock_guard hold{ m_lock };

        m_images.erase(id);
    }

    com_ptr<ID2D1Bitmap1> XamlImages::Bitmap(ID2D1DeviceContext5* context, int id)
    {
        std::function<void(int)> ask;

        {
            std::lock_guard hold{ m_lock };
            auto found = m_images.find(id);

            if (found != m_images.end())
            {
                auto& image = found->second;

                if (image.bitmap)
                {
                    return image.bitmap;
                }

                if (context && !image.pixels.empty())
                {
                    D2D1_BITMAP_PROPERTIES1 how{};

                    how.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                    how.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

                    com_ptr<ID2D1Bitmap1> made;
                    auto const size = D2D1_SIZE_U{
                        static_cast<uint32_t>(image.width),
                        static_cast<uint32_t>(image.height)
                    };

                    if (SUCCEEDED(context->CreateBitmap(
                            size, image.pixels.data(),
                            static_cast<uint32_t>(image.width) * 4, how,
                            made.put())))
                    {
                        image.bitmap = made;
                        return image.bitmap;
                    }
                }

                return nullptr;
            }

            // Asked for once: a screen says an image on every row it
            // covers, and every screen until the pixels arrive says it
            // again.
            auto& waiting = m_images[id];

            if (waiting.asked)
            {
                return nullptr;
            }
            waiting.asked = true;
            ask = m_ask;
        }

        // Outside the lock: what this does is send to Emacs, and it has
        // no business holding up whoever else is taking an image.
        if (ask)
        {
            ask(id);
        }
        return nullptr;
    }

    void XamlImages::Forget()
    {
        std::lock_guard hold{ m_lock };

        for (auto& [id, image] : m_images)
        {
            image.bitmap = nullptr;
        }
    }
}
