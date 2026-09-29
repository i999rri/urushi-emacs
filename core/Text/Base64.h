#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace urusi::core::text
{
    // Base64, as the protocol sends the bytes of a font or an image.
    //
    // Decoded from the line where it lies, so that nothing is made of
    // it on the way: CryptographicBuffer takes a string of the
    // platform's own, which on Windows means the line again as UTF-16,
    // and a font file of eighty megabytes is a hundred and ten
    // megabytes of base64 and twice that as UTF-16.  This reads the
    // bytes the line already holds.

    namespace base64
    {
        // What each character is worth, and -1 for the ones that are
        // worth nothing: the padding, and the newline a line is broken
        // by where anything breaks it.
        inline std::array<signed char, 256> const& Values()
        {
            static std::array<signed char, 256> const values = [] {
                std::array<signed char, 256> made{};
                made.fill(-1);
                char const* const alphabet =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (signed char i = 0; i < 64; ++i)
                {
                    made[static_cast<unsigned char>(alphabet[i])] = i;
                }
                return made;
            }();
            return values;
        }
    }

    // TEXT decoded. A character that is worth nothing is passed over,
    // so that padding and any breaking of the line are no trouble.
    //
    // Four characters at a time down a pointer of its own, rather than
    // a byte at a time onto the end of a vector: a font file is tens of
    // megabytes, and the decoding of it is nearly the whole of what the
    // person waits for while the text in that font goes undrawn.
    inline std::vector<uint8_t> DecodeBase64(std::string_view text)
    {
        auto const& values = base64::Values();
        std::vector<uint8_t> bytes;

        // Three bytes to every four characters, which is as many as it
        // can come to; cut to what it came to at the end.
        bytes.resize(text.size() / 4 * 3 + 3);

        uint8_t* put = bytes.data();
        uint8_t const* const end = put + bytes.size();
        auto const* from = reinterpret_cast<unsigned char const*>(text.data());
        auto const* const last = from + text.size();

        // The four in hand, and how many of them there are: a character
        // worth nothing does not count, so a line broken anywhere still
        // comes out the same.
        signed char four[4]{};
        int many = 0;

        while (from < last)
        {
            signed char const worth = values[*from++];

            if (worth < 0)
            {
                continue;
            }
            four[many++] = worth;
            if (many < 4)
            {
                continue;
            }
            many = 0;
            if (end - put < 3)
            {
                break;
            }

            uint32_t const held = (static_cast<uint32_t>(four[0]) << 18)
                | (static_cast<uint32_t>(four[1]) << 12)
                | (static_cast<uint32_t>(four[2]) << 6)
                | static_cast<uint32_t>(four[3]);

            *put++ = static_cast<uint8_t>(held >> 16);
            *put++ = static_cast<uint8_t>(held >> 8);
            *put++ = static_cast<uint8_t>(held);
        }

        // What the padding left: two characters are one byte and three
        // are two, and one alone is no byte at all.
        if (many >= 2 && end - put >= static_cast<ptrdiff_t>(many) - 1)
        {
            uint32_t const held = (static_cast<uint32_t>(four[0]) << 18)
                | (static_cast<uint32_t>(four[1]) << 12)
                | (static_cast<uint32_t>(four[2]) << 6);

            *put++ = static_cast<uint8_t>(held >> 16);
            if (many == 3)
            {
                *put++ = static_cast<uint8_t>(held >> 8);
            }
        }

        bytes.resize(static_cast<size_t>(put - bytes.data()));
        return bytes;
    }
}
