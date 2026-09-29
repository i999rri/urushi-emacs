#pragma once

#include <string>
#include <string_view>

#include "Text/JsonLine.h"

namespace urushi::core::window
{
    // What a `font' message says, read where it lies.
    //
    // A font comes in two messages: which file it is, and, when the
    // host asks, the file itself as base64.  The second is the largest
    // thing the protocol sends -- a CJK collection is eighty megabytes,
    // which is a hundred and ten of base64 -- and it arrives on the
    // thread that reads from Emacs, which reads nothing else while it
    // is busy.  Parsed into objects it is that line again as UTF-16 and
    // again as a string of the parser's, and the window stops for as
    // long as that takes.
    //
    // So the bytes are left where they are: `bytes' is a view into the
    // line, still base64, for text::DecodeBase64 to read straight from.
    // Nothing here copies anything but the path, which is short.
    struct FontSaid
    {
        int id{ -1 };
        int instance{ -1 };
        int face{ 0 };

        // Which file it is, for the message that says so.
        std::string file;
        long long size{ 0 };
        long long when{ 0 };

        // The file itself, base64, for the message that carries it.
        std::string_view bytes;
        bool hasBytes{ false };
    };

    namespace font
    {
        // Emacs escapes a backslash and a quotation mark in the path
        // and nothing else (hostfont.c), so this puts back those two.
        inline std::string Unescaped(std::string_view text)
        {
            std::string put;

            put.reserve(text.size());
            for (size_t at = 0; at < text.size(); ++at)
            {
                if (text[at] == '\\' && at + 1 < text.size())
                {
                    ++at;
                }
                put.push_back(text[at]);
            }
            return put;
        }

        // Pass over one value, whatever kind it is, leaving AT after
        // it.  Always moves on: a reader that stood still on something
        // it did not know would never reach the end of the line.
        inline void PassValue(std::string_view line, size_t& at)
        {
            namespace json = text::json;

            if (at >= line.size())
            {
                return;
            }
            if (line[at] == '"')
            {
                json::Text(line, at);
                return;
            }
            if (line[at] == '[' || line[at] == '{')
            {
                char const close = line[at] == '[' ? ']' : '}';
                int depth = 0;

                for (; at < line.size(); ++at)
                {
                    if (line[at] == '"')
                    {
                        // A brace inside a string is no brace of the
                        // structure's, so the string is passed whole.
                        json::Text(line, at);
                        --at;
                        continue;
                    }
                    if (line[at] == '[' || line[at] == '{')
                    {
                        ++depth;
                    }
                    else if (line[at] == ']' || line[at] == '}')
                    {
                        --depth;
                        if (depth == 0)
                        {
                            if (line[at] == close)
                            {
                                ++at;
                            }
                            return;
                        }
                    }
                }
                return;
            }

            auto const from = at;
            if (line[at] == '-' || (line[at] >= '0' && line[at] <= '9'))
            {
                json::Number(line, at);
            }
            else
            {
                json::Word(line, at);
            }
            // true, false, null and a number are all read above; a
            // character that is none of them is stepped over, so that
            // nothing can hold the reader in one place.
            if (at == from)
            {
                ++at;
            }
        }
    }

    // Read LINE, one `font' message, into SAID. False if it is no
    // message of one.
    inline bool ReadFont(std::string_view line, FontSaid& said)
    {
        namespace json = text::json;
        size_t at = line.find('{');

        if (at == std::string_view::npos)
        {
            return false;
        }
        ++at;

        while (at < line.size())
        {
            json::PassSpace(line, at);
            if (at >= line.size() || line[at] == '}')
            {
                return said.id >= 0;
            }
            if (line[at] == ',')
            {
                ++at;
                continue;
            }

            auto const key = json::Text(line, at);
            json::PassSpace(line, at);
            if (at >= line.size() || line[at] != ':')
            {
                return false;
            }
            ++at;
            json::PassSpace(line, at);

            if (key == "type")
            {
                if (json::Text(line, at) != "font")
                {
                    return false;
                }
            }
            else if (key == "id")
            {
                said.id = static_cast<int>(json::Number(line, at));
            }
            else if (key == "instance")
            {
                said.instance = static_cast<int>(json::Number(line, at));
            }
            else if (key == "face")
            {
                said.face = static_cast<int>(json::Number(line, at));
            }
            else if (key == "size")
            {
                said.size = json::Number(line, at);
            }
            else if (key == "when")
            {
                said.when = json::Number(line, at);
            }
            else if (key == "file")
            {
                said.file = font::Unescaped(json::Text(line, at));
            }
            else if (key == "bytes")
            {
                said.bytes = json::Text(line, at);
                said.hasBytes = true;
            }
            else
            {
                // A key this does not know, whatever a later protocol
                // adds: passed over as whatever it is, so that the keys
                // after it are still read.
                font::PassValue(line, at);
            }
        }
        return said.id >= 0;
    }
}
