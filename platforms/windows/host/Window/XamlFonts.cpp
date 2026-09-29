#include "pch.h"

#include "XamlFonts.h"

#include <winrt/Windows.Storage.h>

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

namespace urusi::windows::window
{
    void XamlFonts::OnWanting(std::function<void(int)> ask)
    {
        m_ask = std::move(ask);
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
    std::string XamlFonts::MadeOfFile(int id, std::filesystem::path const& path)
    {
        try
        {
            com_ptr<IDWriteFontFile> file;

            check_hresult(Writer()->CreateFontFileReference(
                path.c_str(), nullptr, file.put()));
            return Made(id, file.get());
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
    }

    // The face of a font that is not on a disk of this machine's: the
    // file Emacs read may be on another altogether, as it is when
    // Emacs runs in WSL and nothing has been kept here yet.
    std::string XamlFonts::MadeOfBytes(int id, uint8_t const* bytes, uint32_t length)
    {
        try
        {
            com_ptr<IDWriteFontFile> file;

            check_hresult(Loader()->CreateInMemoryFontFileReference(
                Writer().get(), bytes, length, nullptr, file.put()));
            return Made(id, file.get());
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
    }

    std::string XamlFonts::Made(int id, IDWriteFontFile* file)
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

            // Which font of the file, as Emacs said: a collection
            // holds many -- the one the screen is drawn in holds 48,
            // Japanese and Korean among them -- and the glyph numbers
            // Emacs measured in one are other letters in another.
            auto const which = static_cast<uint32_t>(FaceOf(id));
            if (which >= faces)
            {
                return "font " + std::to_string(id) + " is not the "
                       + std::to_string(which) + " of " + std::to_string(faces);
            }

            IDWriteFontFile* files[] = { file };
            com_ptr<IDWriteFontFace> made;
            check_hresult(Writer()->CreateFontFace(face, 1, files, which,
                                                   DWRITE_FONT_SIMULATIONS_NONE, made.put()));
            m_faces[id] = made;
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }
        return {};
    }

    // Which font of its file the font ID is, or the first where
    // nothing said.
    int XamlFonts::FaceOf(int id) const
    {
        auto found = m_files.find(id);

        return found == m_files.end() ? 0 : found->second.face;
    }

    std::string XamlFonts::Take(urusi::core::window::FontSaid const& said)
    {
        std::scoped_lock held{ m_lock };
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
            auto kept = NameOf(urusi::core::text::FromUtf8(said.file),
                               static_cast<int64_t>(said.size),
                               static_cast<int64_t>(said.when),
                               said.instance);

            m_files[id] = { kept, false, said.face };

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
            return MadeOfFile(id, path);
        }

        // Decoded out of the line itself: see core/Text/Base64.h for
        // what taking it through the platform's own would cost.
        auto const bytes = urusi::core::text::DecodeBase64(said.bytes);
        if (bytes.empty())
        {
            return "font " + std::to_string(id) + " came empty";
        }

        // Kept first, and the face made of what was kept: a face made
        // of these bytes would hold them for as long as it lives, and
        // they are the file over again.  Written whole and then moved
        // into place, so that a run that ends midway leaves nothing a
        // later one would take for the file.
        auto const kept = KeepFile(id, bytes);

        if (!kept.empty())
        {
            return MadeOfFile(id, kept);
        }

        // Nowhere to keep it, or it could not be written: the bytes
        // are all there is, and they are held for as long as the face.
        return MadeOfBytes(id, bytes.data(), static_cast<uint32_t>(bytes.size()));
    }

    // Write BYTES where the font ID is kept, and return where that is,
    // or nothing where it could not be written.
    std::filesystem::path XamlFonts::KeepFile(int id, std::vector<uint8_t> const& bytes)
    {
        auto found = m_files.find(id);

        if (found == m_files.end())
        {
            return {};
        }

        auto where = FontsDirectory();
        if (where.empty())
        {
            return {};
        }

        auto const path = where / found->second.kept;
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
