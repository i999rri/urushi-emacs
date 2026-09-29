#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

namespace urushi::core::text::json
{
    // Reading one line of JSON where it lies, rather than parsing it
    // into objects of its own.
    //
    // These began in the reader of a screen's drawing, which was
    // written after measuring the one that used Windows.Data.Json: see
    // core/Window/DrawReader.h for what that cost.  They are here
    // because the same is true of any message whose values are large or
    // many, and nothing in them is of the drawing.
    //
    // Every one takes the line and an offset into it, and leaves the
    // offset after what it read.

    inline void PassSpace(std::string_view line, size_t& at)
    {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
        {
            ++at;
        }
    }

    // The text of the string that begins at AT, which is its quote.
    inline std::string_view Text(std::string_view line, size_t& at)
    {
        if (at >= line.size() || line[at] != '"')
        {
            return {};
        }

        auto const from = ++at;
        while (at < line.size() && line[at] != '"')
        {
            // Nothing Emacs writes here has an escape in it, but
            // one would otherwise end the string early.
            if (line[at] == '\\' && at + 1 < line.size())
            {
                ++at;
            }
            ++at;
        }

        auto const text = line.substr(from, at - from);
        if (at < line.size())
        {
            ++at;
        }
        return text;
    }

    // The word that begins at AT, which is one of true, false and
    // null: JSON writes these bare, and a reader that took one for
    // a number would take none of it and lose its place.
    inline std::string_view Word(std::string_view line, size_t& at)
    {
        auto const from = at;

        while (at < line.size()
               && ((line[at] >= 'a' && line[at] <= 'z')
                   || (line[at] >= 'A' && line[at] <= 'Z')))
        {
            ++at;
        }
        return line.substr(from, at - from);
    }

    inline long long Number(std::string_view line, size_t& at)
    {
        bool const below = at < line.size() && line[at] == '-';
        long long value = 0;

        if (below)
        {
            ++at;
        }
        while (at < line.size() && line[at] >= '0' && line[at] <= '9')
        {
            value = value * 10 + (line[at++] - '0');
        }
        // A fraction, which only the size of a font has.
        if (at < line.size() && line[at] == '.')
        {
            ++at;
            while (at < line.size() && line[at] >= '0' && line[at] <= '9')
            {
                ++at;
            }
        }
        return below ? -value : value;
    }

    inline double Fraction(std::string_view line, size_t& at)
    {
        auto const from = at;

        Number(line, at);
        return std::strtod(
            std::string{ line.substr(from, at - from) }.c_str(), nullptr);
    }
}
