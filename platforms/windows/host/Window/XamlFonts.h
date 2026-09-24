#pragma once

#include <winrt/Windows.Data.Json.h>

#include <dwrite_3.h>
#include <winrt/base.h>

#include <map>
#include <string>
#include <vector>

namespace urusi::windows::window
{
    // The fonts Emacs draws in, as this window has them.
    //
    // Emacs reads the font files itself and works out which glyph of a
    // file each character comes to, so what it says to draw is a glyph
    // of a file rather than a character of a font: a glyph is numbered
    // by the file it is in and nothing else, and a font found here by
    // name might be another file with other numbers in it.  So the file
    // itself is what arrives, and the faces made here are made from it.
    //
    // Each file is kept beside the application under the name its
    // contents come to, so that a later run has it already.
    class XamlFonts
    {
    public:
        // Take a "font" message: the number Emacs knows a file by and
        // the file itself.  Return what went wrong, or nothing.
        std::string Take(winrt::Windows::Data::Json::JsonObject const& message);

        // The face Emacs knows by ID, or null if there is none.
        winrt::com_ptr<IDWriteFontFace> Face(int id) const;

    private:
        winrt::com_ptr<IDWriteFactory5> Writer();

        // Which file each font is, as Emacs said of it: the name it
        // has where Emacs read it, and what it was when read.
        struct Whence
        {
            std::wstring file;
            int64_t size;
            int64_t when;
        };

        winrt::com_ptr<IDWriteFactory5> m_writer;
        std::map<int, Whence> m_files;
        std::map<int, winrt::com_ptr<IDWriteFontFace>> m_faces;
    };
}
