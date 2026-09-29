#pragma once

#include <cstdint>
#include <string>

namespace urushi::core::text
{
    // UTF-16 and UTF-8, the one to the other, without Windows: the code
    // that uses them is tested where there is none. What is not a
    // character, an unpaired surrogate or a broken sequence, becomes
    // U+FFFD.
    inline std::string ToUtf8(std::wstring const& text)
    {
        std::string out;
        out.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i)
        {
            uint32_t c = static_cast<uint16_t>(text[i]);

            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size())
            {
                uint32_t low = static_cast<uint16_t>(text[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            if (c >= 0xD800 && c <= 0xDFFF)
            {
                c = 0xFFFD;
            }

            if (c < 0x80)
            {
                out += static_cast<char>(c);
            }
            else if (c < 0x800)
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += static_cast<char>(0xE0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    inline std::wstring FromUtf8(std::string const& text)
    {
        std::wstring out;
        out.reserve(text.size());

        for (size_t i = 0; i < text.size();)
        {
            auto byte = static_cast<uint8_t>(text[i]);
            uint32_t c = 0xFFFD;
            size_t length = 1;

            if (byte < 0x80)
            {
                c = byte;
            }
            else if ((byte & 0xE0) == 0xC0) { c = byte & 0x1F; length = 2; }
            else if ((byte & 0xF0) == 0xE0) { c = byte & 0x0F; length = 3; }
            else if ((byte & 0xF8) == 0xF0) { c = byte & 0x07; length = 4; }

            if (length > 1)
            {
                if (i + length > text.size())
                {
                    c = 0xFFFD;
                    length = 1;
                }
                else
                {
                    for (size_t k = 1; k < length; ++k)
                    {
                        auto next = static_cast<uint8_t>(text[i + k]);
                        if ((next & 0xC0) != 0x80)
                        {
                            c = 0xFFFD;
                            length = k;
                            break;
                        }
                        c = (c << 6) | (next & 0x3F);
                    }
                }
            }
            i += length;

            if (c >= 0x10000)
            {
                c -= 0x10000;
                out += static_cast<wchar_t>(0xD800 + (c >> 10));
                out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
            }
            else
            {
                out += static_cast<wchar_t>(c);
            }
        }
        return out;
    }
}
