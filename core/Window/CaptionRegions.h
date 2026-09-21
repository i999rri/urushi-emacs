#pragma once

#include "Window/FrameSizes.h"

#include <optional>
#include <vector>

namespace urusi::core::window
{
    // Where something is on the window, in XAML's units, from the corner
    // of the window's client area.
    struct Area
    {
        double x{ 0 };
        double y{ 0 };
        double width{ 0 };
        double height{ 0 };
    };

    // The parts of the window Windows treats as its own: the caption, by
    // which the window is moved and which maximizes it when clicked
    // twice, and what lies in it but is to be clicked like the rest of
    // the window.
    struct INonClientRegions
    {
        virtual ~INonClientRegions() = default;

        virtual void Set(PixelRect caption, std::vector<PixelRect> const& passthrough) = 0;
        virtual void Clear() = 0;
    };

    // The caption of a window whose title bar Lisp drew: the element it
    // named urusi-titlebar is the caption, and the controls on it are
    // left to be clicked. With no such element, or while the window has
    // a title bar of its own, there is none.
    //
    // Windows is told only when that changes: the window tells this each
    // time anything is resized, which is far more often.
    class CaptionRegions
    {
    public:
        explicit CaptionRegions(INonClientRegions& windows) : m_windows(windows) {}

        // TITLEBAR is where the title bar is, if there is one, and
        // CONTROLS where what is to be clicked on it is; SCALE is how
        // many pixels of the screen make one of XAML's.
        void Update(std::optional<Area> titlebar, std::vector<Area> const& controls,
                    double scale)
        {
            if (!titlebar)
            {
                if (!m_cleared)
                {
                    m_windows.Clear();
                    m_cleared = true;
                    m_caption.reset();
                    m_passthrough.clear();
                }
                return;
            }

            auto caption = Pixels(*titlebar, scale);
            std::vector<PixelRect> passthrough;
            passthrough.reserve(controls.size());
            for (auto const& control : controls)
            {
                passthrough.push_back(Pixels(control, scale));
            }

            if (!m_cleared && m_caption == caption && m_passthrough == passthrough)
            {
                return;
            }

            m_windows.Set(caption, passthrough);
            m_cleared = false;
            m_caption = caption;
            m_passthrough = std::move(passthrough);
        }

    private:
        static PixelRect Pixels(Area const& area, double scale) noexcept
        {
            return ToPixels(area.x, area.y, area.width, area.height, scale);
        }

        INonClientRegions& m_windows;

        // What Windows was last told. Neither, to begin with: it is told
        // the first time, whatever that is.
        bool m_cleared{ false };
        std::optional<PixelRect> m_caption;
        std::vector<PixelRect> m_passthrough;
    };
}
