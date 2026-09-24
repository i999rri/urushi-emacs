#include "pch.h"

#include "XamlFonts.h"

#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "dwrite.lib")

using namespace winrt;

namespace urusi::windows::window
{
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

    std::string XamlFonts::Take(Windows::Data::Json::JsonObject const& message)
    {
        int const id = static_cast<int>(message.GetNamedNumber(L"id", -1));
        if (id < 0)
        {
            return "no number for the font";
        }

        // Which file the font is comes first and the file itself only
        // if it is asked for, so a message with no bytes in it is the
        // first of the two: what is in it is written down, and nothing
        // is asked for until there is something to draw from it.
        if (!message.HasKey(L"bytes"))
        {
            m_files[id] = { std::wstring{ message.GetNamedString(L"file", L"") },
                            static_cast<int64_t>(message.GetNamedNumber(L"size", 0)),
                            static_cast<int64_t>(message.GetNamedNumber(L"when", 0)) };
            return {};
        }

        auto bytes = Windows::Security::Cryptography::CryptographicBuffer::
            DecodeFromBase64String(message.GetNamedString(L"bytes", L""));
        if (bytes.Length() == 0)
        {
            return "font " + std::to_string(id) + " came empty";
        }

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
                writer.get(), bytes.data(), bytes.Length(), nullptr, file.put()));

            BOOL supported{};
            DWRITE_FONT_FILE_TYPE kind{};
            DWRITE_FONT_FACE_TYPE face{};
            uint32_t faces{};
            check_hresult(file->Analyze(&supported, &kind, &face, &faces));
            if (!supported || faces == 0)
            {
                return "font " + std::to_string(id) + " is of no kind DirectWrite knows";
            }

            IDWriteFontFile* files[] = { file.get() };
            com_ptr<IDWriteFontFace> made;
            check_hresult(writer->CreateFontFace(face, 1, files, 0,
                                                 DWRITE_FONT_SIMULATIONS_NONE, made.put()));
            m_faces[id] = made;
        }
        catch (hresult_error const& error)
        {
            return "font " + std::to_string(id) + ": " + to_string(error.message());
        }

        return {};
    }

    com_ptr<IDWriteFontFace> XamlFonts::Face(int id) const
    {
        auto found = m_faces.find(id);

        return found == m_faces.end() ? nullptr : found->second;
    }
}
