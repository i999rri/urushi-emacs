#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace urusi::core::window
{
    // XAML counts in 96ths of an inch, and Emacs and Windows in the
    // pixels of the screen; SCALE is how many of those make one of
    // XAML's.
    inline int32_t ToPixels(double value, double scale) noexcept
    {
        return static_cast<int32_t>(std::lround(value * scale));
    }

    struct PixelRect
    {
        int32_t x{ 0 };
        int32_t y{ 0 };
        int32_t width{ 0 };
        int32_t height{ 0 };

        bool operator==(PixelRect const&) const = default;
    };

    inline PixelRect ToPixels(double x, double y, double width, double height,
                              double scale) noexcept
    {
        return { ToPixels(x, scale), ToPixels(y, scale),
                 ToPixels(width, scale), ToPixels(height, scale) };
    }

    struct PixelSize
    {
        int32_t width{ 0 };
        int32_t height{ 0 };

        bool operator==(PixelSize const&) const = default;
    };

    // How big each frame was last told it is, so that it is told only
    // when that changes. Emacs lays its text out again each time it is
    // told, and the elements frames are shown in are measured far more
    // often than they change.
    //
    // A frame is known by an id: the panel's for the frame of a panel,
    // and the empty one for the frame the window shows.
    class FrameSizes
    {
    public:
        // The size to tell the frame ID, whose element is WIDTH by
        // HEIGHT in XAML's units, or nothing: the same as it was told,
        // or no size at all, as an element not laid out yet has.
        std::optional<PixelSize> Resized(std::wstring const& id, double width, double height,
                                         double scale)
        {
            PixelSize size{ ToPixels(width, scale), ToPixels(height, scale) };
            if (size.width <= 0 || size.height <= 0)
            {
                return std::nullopt;
            }

            auto& told = m_told[id];
            if (told == size)
            {
                return std::nullopt;
            }
            told = size;
            return size;
        }

    private:
        std::map<std::wstring, PixelSize> m_told;
    };
}
