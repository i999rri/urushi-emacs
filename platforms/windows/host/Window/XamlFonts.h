#pragma once

#include <dwrite_3.h>
#include <winrt/base.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

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
        ~XamlFonts();

        // Ask Emacs for the file of the font it knows by this number.
        // Called where a face is wanted and the file is not here.
        void OnWanting(std::function<void(int)> ask);

        // Say that a face has been made that was not there before, so
        // that whatever was left undrawn for want of it can be drawn.
        // Called on the thread the files are taken on.
        void OnMade(std::function<void()> again);

        // Take a "font" message that carries the file, on a thread of
        // this window's own rather than where it arrived.
        //
        // The file is tens of megabytes: read, decoded, written and
        // made into a face where the messages are read, it would hold
        // that thread -- and so everything Emacs says next -- for as
        // long as all that takes.  Nothing is drawn in the font until
        // the face is there, which the drawing is written to bear, and
        // OnMade says when it is.
        void Later(std::string line);

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
        winrt::com_ptr<IDWriteInMemoryFontFileLoader> Loader();

        // Make the face of ID out of FILE, whichever way the file was
        // come by.
        std::string Made(int id, IDWriteFontFile* file);
        // Out of a file kept here, which DirectWrite reads itself.
        std::string MadeOfFile(int id, std::filesystem::path const& path);
        // Out of bytes this window holds, for a file it has nowhere to
        // keep and a machine it cannot read the file on.
        std::string MadeOfBytes(int id, uint8_t const* bytes, uint32_t length);

        // Keep BYTES as the file of the font ID, and say where.
        std::filesystem::path KeepFile(int id, std::vector<uint8_t> const& bytes);
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

        // Take what is waiting, one after another, until there is
        // nothing and this window is going.
        void Work();

        mutable std::mutex m_lock;
        std::function<void(int)> m_ask;
        std::function<void()> m_again;
        winrt::com_ptr<IDWriteFactory5> m_writer;
        winrt::com_ptr<IDWriteInMemoryFontFileLoader> m_loader;

        // The files waiting to be taken, and the thread that takes
        // them: one at a time, a file at once being tens of megabytes
        // and two of them twice that.
        std::mutex m_waitingLock;
        std::condition_variable m_waking;
        std::deque<std::string> m_waiting;
        std::thread m_worker;
        bool m_going{ false };
        std::map<int, Whence> m_files;
        std::map<int, winrt::com_ptr<IDWriteFontFace>> m_faces;
    };
}
