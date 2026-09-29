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
    inline std::vector<uint8_t> DecodeBase64(std::string_view text)
    {
        auto const& values = base64::Values();
        std::vector<uint8_t> bytes;

        // Three bytes to every four characters, which is what it comes
        // to when nothing in it is passed over.
        bytes.reserve(text.size() / 4 * 3);

        uint32_t held = 0;
        int bits = 0;
        for (char const character : text)
        {
            signed char const worth = values[static_cast<unsigned char>(character)];

            if (worth < 0)
            {
                continue;
            }
            held = (held << 6) | static_cast<uint32_t>(worth);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                bytes.push_back(static_cast<uint8_t>((held >> bits) & 0xFF));
            }
        }
        return bytes;
    }
}
