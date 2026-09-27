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

        // An image: which one, as Emacs numbers it, and how big it is
        // before anything is done to it.  The box is what it is drawn
        // within, so a tall image is one command to a row, each naming
        // the same image within another box.
        //
        // matrix carries the image from its own corner to where this
        // row's part of it goes, scaled and turned as the image asked
        // to be: apply it, draw the image at its own size, and leave
        // out what falls outside the box.  Emacs worked all of that
        // out; there is nothing here to decide.
        int image{};
        int imageWidth{};
        int imageHeight{};
        double matrix[6]{ 1, 0, 0, 1, 0, 0 };
        // Whether the pixels are to be blended, which is what keeps an
        // image drawn smaller than it is from looking like a comb.
        bool smooth{};
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
