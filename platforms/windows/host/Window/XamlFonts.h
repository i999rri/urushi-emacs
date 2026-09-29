#pragma once

#include <dwrite_3.h>
#include <winrt/base.h>

#include <functional>
#include <mutex>
#include <map>
#include <string>

#include "Window/FontReader.h"

namespace urusi::windows::window
{
    // The fonts Emacs draws in, as this window has them.
    //
    // Emacs reads the font files itself and works out which glyph of a
    // file each character comes to, so what it says to draw is a glyph
    // of a file rather than a character of a font: a glyph is numbered
    // by the file it is in and nothing else, and a font found here by
    // name would be another file, numbering them otherwise.  So the
    // file itself is what arrives, and the faces are made from it.
    //
    // A file is kept once it has arrived, under the name what Emacs
    // said of it comes to, so that a later run asks for none of it.
    //
    // The file arrives on the thread that reads from Emacs, and is
    // made into a face there: it may be tens of megabytes, and doing
    // that where the window is drawn would stop it for as long.
    // Nothing here touches an element, so only these need locking.
    class XamlFonts
    {
    public:
        // Ask Emacs for the file of the font it knows by this number.
        // Called where a face is wanted and the file is not here.
        void OnWanting(std::function<void(int)> ask);

        // Take a "font" message, read where it lay: which file a font
        // is, or the file.  Return what went wrong, or nothing.
        //
        // It is read rather than parsed because the file may be eighty
        // megabytes; see core/Window/FontReader.h.
        std::string Take(urusi::core::window::FontSaid const& said);

        // The face Emacs knows by ID, or null if there is none yet, in
        // which case the file is asked for.
        winrt::com_ptr<IDWriteFontFace> Face(int id);

    private:
        winrt::com_ptr<IDWriteFactory5> Writer();
        // Make the face of ID out of BYTES, LENGTH of them.
        std::string Made(int id, uint8_t const* bytes, uint32_t length);
        int FaceOf(int id) const;

        // Which file each font is, as Emacs said of it, and what this
        // window keeps that file under.
        struct Whence
        {
            std::wstring kept;
            bool asked;
            // Which font of the file: a collection holds many, and the
            // glyph numbers of one are the glyphs of another.
            int face;
        };

        mutable std::mutex m_lock;
        std::function<void(int)> m_ask;
        winrt::com_ptr<IDWriteFactory5> m_writer;
        std::map<int, Whence> m_files;
        std::map<int, winrt::com_ptr<IDWriteFontFace>> m_faces;
    };
}
