#pragma once

#include <dwrite_3.h>
#include <winrt/base.h>

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "Window/XamlFonts.h"

namespace urusi::windows::window
{
    // The letters the text is drawn with, rasterized once and kept.
    //
    // Emacs says which glyph of which file goes where; what a glyph
    // looks like is the same every time it is drawn, so it is worked
    // out once.  Without that the screen would be rasterized afresh
    // for every frame, which is the work Emacs was handing over in the
    // first place.
    //
    // DirectWrite does the rasterizing, so the text of the editor is
    // drawn by the same hand as the text around it.
    //
    // Of the user interface thread, like the picture it draws into.
    class XamlGlyphs
    {
    public:
        explicit XamlGlyphs(std::shared_ptr<XamlFonts> fonts)
            : m_fonts(std::move(fonts))
        {
        }

        // How much of each pixel one glyph covers, a row at a time,
        // from LEFT and TOP of where the pen was on the baseline.
        struct Raster
        {
            int left{};
            int top{};
            int width{};
            int height{};
            std::vector<uint8_t> coverage;
        };

        // The glyph numbered ID of the font Emacs knows as FONT, drawn
        // SIZE pixels tall; null while the font's file is not here, in
        // which case it has been asked for.
        Raster const* Of(int font, double size, uint16_t id);

    private:
        winrt::com_ptr<IDWriteFactory> Writer();

        std::shared_ptr<XamlFonts> m_fonts;
        winrt::com_ptr<IDWriteFactory> m_writer;
        // Keyed by the font, the size it is drawn at and the glyph.
        // The size is rounded to a quarter of a pixel: Emacs asks for
        // the same few sizes, and two that differ by less than that
        // rasterize the same.
        std::map<std::tuple<int, int, uint16_t>, Raster> m_rasters;
    };
}
