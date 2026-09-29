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
    // What a font file is kept under here: what Emacs said of it, run
    // together into one name.  Two files of the same name, length and
    // time are the same file; one that has changed since is another,
    // and comes again under another name.
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

        wchar_t name[32]{};
        swprintf_s(name, L"%016llx.font", static_cast<unsigned long long>(hash));
        return name;
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

    std::string XamlFonts::Made(int id, uint8_t const* bytes, uint32_t length)
    {
        try
        {
            auto writer = Writer();

            // From the bytes rather than from a file of its own: the
            // file Emacs read may be on another machine altogether, as
            // it is when Emacs runs in WSL.
            com_ptr<IDWriteInMemoryFontFileLoader> loader;
            check_hresult(writer->CreateInMemoryFontFileLoader(loader.put()));
            check_hresult(writer->RegisterFontFileLoader(loader.get()));

            com_ptr<IDWriteFontFile> file;
            check_hresult(loader->CreateInMemoryFontFileReference(
                writer.get(), bytes, length, nullptr, file.put()));

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

            IDWriteFontFile* files[] = { file.get() };
            com_ptr<IDWriteFontFace> made;
            check_hresult(writer->CreateFontFace(face, 1, files, which,
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

            std::ifstream file{ where / kept, std::ios::binary | std::ios::ate };
            if (!file)
            {
                return {};
            }

            std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (!file)
            {
                return {};
            }
            return Made(id, bytes.data(), static_cast<uint32_t>(bytes.size()));
        }

        // Decoded out of the line itself: see core/Text/Base64.h for
        // what taking it through the platform's own would cost.
        auto const bytes = urusi::core::text::DecodeBase64(said.bytes);
        if (bytes.empty())
        {
            return "font " + std::to_string(id) + " came empty";
        }

        if (auto why = Made(id, bytes.data(), static_cast<uint32_t>(bytes.size()));
            !why.empty())
        {
            return why;
        }

        // Kept, so that a later run asks for none of it.
        if (auto found = m_files.find(id); found != m_files.end())
        {
            auto where = FontsDirectory();

            if (!where.empty())
            {
                std::ofstream file{ where / found->second.kept, std::ios::binary };

                file.write(reinterpret_cast<char const*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()));
            }
        }
        return {};
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
