#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace urusi::core::window
{
    // One thing Emacs says to draw.
    //
    // Emacs used to hand over the pixels it had drawn; this is what it
    // drew instead, said in the order it drew it.  Order is the whole
    // of it: the text goes over the background that was filled before
    // it, so drawing these in any other order shows something else.
    enum class DrawOp
    {
        Fill,
        Rectangle,
        Line,
        // Pixels already on the screen, moved: what a window scrolling
        // comes to.
        Copy,
        Clip,
        Unclip,
        Glyphs,
        // Pixels Emacs decoded, which it sends once and says by number
        // however often it is drawn.
        Image,
    };

    struct DrawCommand
    {
        DrawOp op{ DrawOp::Fill };

        // The box it is in.  A line runs from x,y to width,height,
        // which are the far corner rather than a size; a copy goes to
        // toY.  A run of glyphs sits on the baseline y.
        int x{};
        int y{};
        int width{};
        int height{};
        int toY{};
        // 0x00RRGGBB, as Emacs keeps a color.
        uint32_t color{};

        // A run of glyphs: which font file, how big it is drawn, and
        // for each glyph its number in that file and where it goes.
        // The numbers are the file's own, which is why Emacs hands the
        // file over rather than naming the font.
        int font{};
        double size{};
        std::vector<uint16_t> ids;
        std::vector<int> xs;

        // An image: which one, as Emacs numbers it, and where in it the
        // part drawn begins.  The box is where that part goes, so a
        // tall image is one command to a row, each naming the same
        // image with another corner of it.
        int image{};
        int fromX{};
        int fromY{};
    };

    // A whole screen's worth, between a "begin" and an "end".  Nothing
    // is shown until the end arrives: a screen drawn halfway is a
    // screen no one meant.
    struct DrawFrame
    {
        std::wstring frame;
        int width{};
        int height{};
        std::vector<DrawCommand> commands;
    };
}
