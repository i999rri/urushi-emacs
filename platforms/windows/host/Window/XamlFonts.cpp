#include "pch.h"

#include "XamlFonts.h"

#include <winrt/Windows.Storage.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Text/Base64.h"
#include "Text/Utf.h"

#pragma comment(lib, "dwrite.lib")

using namespace winrt;

namespace
{
    // The name of the file itself, for the kept one to be read by a
    // person: the last part of the path, with whatever Windows will
    // not have in a name taken out.  Empty where there is none to take.
    std::wstring ReadableName(std::wstring const& file)
    {
        auto const slash = file.find_last_of(L"/\\");
        auto const last = slash == std::wstring::npos ? file : file.substr(slash + 1);
        std::wstring name;

        for (wchar_t letter : last)
        {
            if (letter < 0x20 || wcschr(L"<>:\"/\\|?*", letter) != nullptr)
            {
                continue;
            }
            name.push_back(letter);
            // Long enough to say which font it is, short enough that
            // the whole name is under what a path may hold.
            if (name.size() >= 64)
            {
                break;
            }
        }
        return name;
    }

    // What a font file is kept under here: the name of the file, so
    // that what is kept can be read, and what Emacs said of it run
    // together into a hash, which is what says whether it is the same
    // file.
    //
    // The hash cannot be left out for the name alone.  Emacs numbers a
    // glyph by the file it is in, so a font file that has changed --
    // another version of the same family -- numbers them otherwise,
    // and one taken for the other draws other letters with nothing to
    // say it did.  The name of the family would not do either: it is
    // not the same on every system (w32 says "Iosevka NFM" where the
    // file says "Iosevka Nerd Font Mono"), and a collection holds many
    // families in the one file.
    std::wstring NameOf(std::wstring const& file, int64_t length, int64_t when,
                        int instance)
    {
        uint64_t hash = 14695981039346656037ull;
        auto eat = [&hash](uint64_t value) {
            for (int at = 0; at < 8; at++)
            {
                hash = (hash ^ ((value >> (at * 8)) & 0xFF)) * 1099511628211ull;
            }
        };

        for (wchar_t letter : file)
        {
            eat(static_cast<uint64_t>(letter));
        }
        eat(static_cast<uint64_t>(length));
        eat(static_cast<uint64_t>(when));
        eat(static_cast<uint64_t>(instance));

        wchar_t which[32]{};
        swprintf_s(which, L"-%016llx.font", static_cast<unsigned long long>(hash));

        auto readable = ReadableName(file);
        return readable.empty() ? std::wstring{ which + 1 } : readable + which;
    }

    // Where the font files this window has been given are kept, made if
    // it is not there.  Empty if there is nowhere to keep them.
    std::filesystem::path FontsDirectory()
    {
        try
        {
            std::filesystem::path where{
                std::wstring{ Windows::Storage::ApplicationData::Current().LocalFolder().Path() }
            };

            where /= L"fonts";
            std::filesystem::create_directories(where);
            return where;
        }
        catch (...)
        {
            return {};
        }
    }
}

namespace urushi::windows::window
{
    XamlFonts::~XamlFonts()
    {
        {
            std::scoped_lock held{ m_waitingLock };

            m_going = false;
            m_waiting.clear();
        }
        m_waking.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void XamlFonts::OnWanting(std::function<void(int)> ask)
    {
        m_ask = std::move(ask);
    }

    void XamlFonts::OnMade(std::function<void()> again)
    {
        m_again = std::move(again);
    }

    void XamlFonts::OnSaying(std::function<void(std::string)> say)
    {
        m_say = std::move(say);
    }

    void XamlFonts::Later(std::string line)
    {
        std::unique_lock held{ m_waitingLock };

        if (!m_worker.joinable())
        {
            m_going = true;
            m_worker = std::thread{ [this] { Work(); } };
        }
        m_waiting.push_back(std::move(line));
        held.unlock();
        m_waking.notify_one();
    }

    void XamlFonts::Work()
    {
        for (;;)
        {
            std::string line;
            {
                std::unique_lock held{ m_waitingLock };

                m_waking.wait(held, [this] { return !m_waiting.empty() || !m_going; });
                if (m_waiting.empty())
                {
                    return;
                }
                line = std::move(m_waiting.front());
                m_waiting.pop_front();
            }

            urushi::core::window::FontSaid said;
            if (!urushi::core::window::ReadFont(line, said))
            {
                continue;
            }

            Cost cost;
            auto const began = std::chrono::steady_clock::now();
            auto const why = Take(said, &cost);
            auto const took = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - began).count();

            if (m_say)
            {
                // What this cost is what the person waited: the text in
                // this font is not drawn until it is done.  Said in its
                // parts, so that what is worth mending is known and not
                // guessed at.
                char note[192]{};

                sprintf_s(note,
                          "font %d: %.1f MB taken in %.0f ms"
                          " (decode %.0f, kept %.0f, face %.0f)%s\n",
                          said.id,
                          static_cast<double>(said.bytes.size()) * 3 / 4 / (1024 * 1024),
                          took, cost.decode, cost.kept, cost.face,
                          why.empty() ? "" : ", and not made");
                m_say(note);
            }
            if (!why.empty())
            {
                if (m_say)
                {
                    m_say("font: " + why + "\n");
                }
                continue;
            }
            if (m_again)
            {
                // A face that was not there before: what was left
                // undrawn for want of it is to be drawn now.
                m_again();
            }
        }
    }

    com_ptr<IDWriteFactory5> XamlFonts::Writer()
    {
        if (!m_writer)
        {
            check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                              __uuidof(IDWriteFactory5),
                                              reinterpret_cast<IUnknown**>(m_writer.put())));
        }
        return m_writer;
    }

    // The loader that holds a font this window was given in memory,
    // made once: registering one for every font would leave as many
    // behind, none of them let go of.
    com_ptr<IDWriteInMemoryFontFileLoader> XamlFonts::Loader()
    {
        if (!m_loader)
        {
            auto writer = Writer();

            check_hresult(writer->CreateInMemoryFontFileLoader(m_loader.put()));
            check_hresult(writer->RegisterFontFileLoader(m_loader.get()));
        }
        return m_loader;
    }

    // The face of a font kept here, read from the file rather than
    // held in memory: DirectWrite maps the file and reads the parts it
    // wants, where bytes handed to it are a font file this window must
    // keep whole for as long as the face lives -- eighty megabytes for
    // a CJK collection, and again for every run.
    std::string XamlFonts::MadeOfFile(int id, std::filesystem::path const& path, int which)
    {
        try
        {
            com_ptr<IDWriteFontFile> file;

            check_hresult(Writer()->CreateFontFileReference(
                path.c_str(), nullptr, file.put()));
            return Made(id, file.get(), which);
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
    }

    // The face of a font that is not on a disk of this machine's: the
    // file Emacs read may be on another altogether, as it is when
    // Emacs runs in WSL and nothing has been kept here yet.
    std::string XamlFonts::MadeOfBytes(int id, uint8_t const* bytes, uint32_t length,
                                       int which)
    {
        try
        {
            com_ptr<IDWriteFontFile> file;

            check_hresult(Loader()->CreateInMemoryFontFileReference(
                Writer().get(), bytes, length, nullptr, file.put()));
            return Made(id, file.get(), which);
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
    }

    std::string XamlFonts::Made(int id, IDWriteFontFile* file, int which)
    {
        try
        {
            BOOL supported{};
            DWRITE_FONT_FILE_TYPE kind{};
            DWRITE_FONT_FACE_TYPE face{};
            uint32_t faces{};
            check_hresult(file->Analyze(&supported, &kind, &face, &faces));
            if (!supported || faces == 0)
            {
                return "font " + std::to_string(id) + " is of no kind DirectWrite knows";
            }

            // WHICH font of the file, as Emacs said: a collection
            // holds many -- the one the screen is drawn in holds 48,
            // Japanese and Korean among them -- and the glyph numbers
            // Emacs measured in one are other letters in another.
            //
            // Handed in rather than looked up, so that nothing of this
            // is done holding the lock: the drawing asks for a face
            // while this runs, and reading a font file of tens of
            // megabytes with the lock held would stop it for as long.
            auto const one = static_cast<uint32_t>(which);
            if (one >= faces)
            {
                return "font " + std::to_string(id) + " is not the "
                       + std::to_string(one) + " of " + std::to_string(faces);
            }

            IDWriteFontFile* files[] = { file };
            com_ptr<IDWriteFontFace> made;
            check_hresult(Writer()->CreateFontFace(face, 1, files, one,
                                                   DWRITE_FONT_SIMULATIONS_NONE, made.put()));

            std::scoped_lock held{ m_lock };
            m_faces[id] = made;
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
        return {};
    }


    // Take a font message.  The lock is held only where the maps are
    // read or written, and never while a file is decoded, written or
    // read into a face: the drawing asks for a face on its own thread,
    // and a lock held through any of that would stop the window for as
    // long as it took -- which is the whole of why this is on a thread
    // of its own.
    std::string XamlFonts::Take(urushi::core::window::FontSaid const& said, Cost* cost)
    {
        auto const since = [](std::chrono::steady_clock::time_point from) {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - from).count();
        };
        int const id = said.id;
        if (id < 0)
        {
            return "no number for the font";
        }

        // Which file the font is comes first and the file itself only
        // if it is asked for, so a message with no bytes in it is the
        // first of the two: what is kept here of that file from a run
        // before this one is looked for, and nothing is asked for
        // until there is something to draw from it.
        if (!said.hasBytes)
        {
            auto kept = NameOf(urushi::core::text::FromUtf8(said.file),
                               static_cast<int64_t>(said.size),
                               static_cast<int64_t>(said.when),
                               said.instance);

            {
                std::scoped_lock held{ m_lock };

                m_files[id] = { kept, false, said.face };
            }

            auto where = FontsDirectory();
            if (where.empty())
            {
                return {};
            }

            std::error_code failed;
            auto const path = where / kept;
            if (!std::filesystem::exists(path, failed))
            {
                // Not kept from a run before this one: nothing is asked
                // for until there is something to draw from it.
                return {};
            }
            return MadeOfFile(id, path, said.face);
        }

        // Decoded out of the line itself: see core/Text/Base64.h for
        // what taking it through the platform's own would cost.
        auto began = std::chrono::steady_clock::now();
        auto const bytes = urushi::core::text::DecodeBase64(said.bytes);
        if (cost) { cost->decode = since(began); }
        if (bytes.empty())
        {
            return "font " + std::to_string(id) + " came empty";
        }

        // What was said of this font when it was named, which is all
        // the maps are wanted for here.
        std::wstring kept;
        int which = 0;
        {
            std::scoped_lock held{ m_lock };

            if (auto found = m_files.find(id); found != m_files.end())
            {
                kept = found->second.kept;
                which = found->second.face;
            }
        }

        // Kept first, and the face made of what was kept: a face made
        // of these bytes would hold them for as long as it lives, and
        // they are the file over again.  Written whole and then moved
        // into place, so that a run that ends midway leaves nothing a
        // later one would take for the file.
        began = std::chrono::steady_clock::now();
        auto const path = KeepFile(kept, bytes);
        if (cost) { cost->kept = since(began); }

        began = std::chrono::steady_clock::now();
        if (!path.empty())
        {
            auto why = MadeOfFile(id, path, which);
            if (cost) { cost->face = since(began); }
            return why;
        }

        // Nowhere to keep it, or it could not be written: the bytes
        // are all there is, and they are held for as long as the face.
        auto why = MadeOfBytes(id, bytes.data(), static_cast<uint32_t>(bytes.size()), which);
        if (cost) { cost->face = since(began); }
        return why;
    }

    // Write BYTES under the name KEPT, and return where that is, or
    // nothing where it could not be written.
    std::filesystem::path XamlFonts::KeepFile(std::wstring const& kept,
                                              std::vector<uint8_t> const& bytes)
    {
        if (kept.empty())
        {
            return {};
        }

        auto where = FontsDirectory();
        if (where.empty())
        {
            return {};
        }

        auto const path = where / kept;
        auto const partly = path.wstring() + L".part";

        {
            std::ofstream file{ partly, std::ios::binary };

            if (!file)
            {
                return {};
            }
            file.write(reinterpret_cast<char const*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                return {};
            }
        }

        std::error_code failed;
        std::filesystem::rename(partly, path, failed);
        if (failed)
        {
            std::filesystem::remove(partly, failed);
            return {};
        }
        return path;
    }

    com_ptr<IDWriteFontFace> XamlFonts::Face(int id)
    {
        std::scoped_lock held{ m_lock };

        if (auto found = m_faces.find(id); found != m_faces.end())
        {
            return found->second;
        }

        // Asked for once: Emacs answers with the file, and until it
        // does there is nothing to draw the glyphs of it with.
        if (auto found = m_files.find(id); found != m_files.end() && !found->second.asked)
        {
            found->second.asked = true;
            if (m_ask)
            {
                m_ask(id);
            }
        }
        return nullptr;
    }
}
