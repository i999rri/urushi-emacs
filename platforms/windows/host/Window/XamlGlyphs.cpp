#include "pch.h"

#include "Window/XamlGlyphs.h"

using namespace winrt;

namespace urushi::windows::window
{
    com_ptr<IDWriteFactory> XamlGlyphs::Writer()
    {
        if (!m_writer)
        {
            check_hresult(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<::IUnknown**>(m_writer.put())));
        }
        return m_writer;
    }

    XamlGlyphs::Raster const* XamlGlyphs::Of(int font, double size, uint16_t id)
    {
        auto const quarters = static_cast<int>(size * 4 + 0.5);
        auto const key = std::make_tuple(font, quarters, id);

        if (auto found = m_rasters.find(key); found != m_rasters.end())
        {
            return found->second.coverage.empty() ? nullptr : &found->second;
        }

        auto face = m_fonts ? m_fonts->Face(font) : nullptr;
        if (!face)
        {
            // Not kept: the file is on its way, and the next screen
            // drawn is to find it here.
            return nullptr;
        }

        float const advance = 0.0f;
        DWRITE_GLYPH_OFFSET const offset{ 0.0f, 0.0f };
        DWRITE_GLYPH_RUN run{};

        run.fontFace = face.get();
        run.fontEmSize = static_cast<float>(quarters) / 4.0f;
        run.glyphCount = 1;
        run.glyphIndices = &id;
        run.glyphAdvances = &advance;
        run.glyphOffsets = &offset;

        com_ptr<IDWriteGlyphRunAnalysis> analysis;
        if (FAILED(Writer()->CreateGlyphRunAnalysis(
                &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL,
                DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f, analysis.put())))
        {
            m_rasters[key] = Raster{};
            return nullptr;
        }

        // The three-byte texture, not the one-byte one: DirectWrite
        // gives an empty box for that one unless the text is drawn
        // with no antialiasing at all.  The three are how much of the
        // pixel each of its three parts covers, and their middle is
        // how much of the pixel is covered.
        RECT bounds{};
        if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1,
                                                   &bounds))
            || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            // A space covers nothing, which is not a mistake: it is
            // kept so that it is not worked out again.
            m_rasters[key] = Raster{};
            return nullptr;
        }

        Raster raster;
        raster.left = bounds.left;
        raster.top = bounds.top;
        raster.width = bounds.right - bounds.left;
        raster.height = bounds.bottom - bounds.top;

        std::vector<uint8_t> thirds(static_cast<size_t>(raster.width)
                                    * raster.height * 3);
        if (FAILED(analysis->CreateAlphaTexture(
                DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, thirds.data(),
                static_cast<uint32_t>(thirds.size()))))
        {
            m_rasters[key] = Raster{};
            return nullptr;
        }

        raster.coverage.resize(static_cast<size_t>(raster.width) * raster.height);
        for (size_t at = 0; at < raster.coverage.size(); ++at)
        {
            raster.coverage[at] = static_cast<uint8_t>(
                (thirds[at * 3] + thirds[at * 3 + 1] + thirds[at * 3 + 2]) / 3);
        }

        auto const& kept = (m_rasters[key] = std::move(raster));
        return &kept;
    }
}
