#pragma once

#include <algorithm>
#include <optional>
#include <string>

namespace urusi
{
    // A splitter Lisp put between two parts of a layout. It is named
    // urusi-splitter:DIRECTION:BEFORE:AFTER, DIRECTION being h for one
    // between parts side by side and v for one between parts one above
    // the other; BEFORE and AFTER are the ids of the parts.
    struct SplitterName
    {
        bool horizontal{ false };
        std::wstring before;
        std::wstring after;
    };

    // NAME read as a splitter's, or nothing if it is not one.
    inline std::optional<SplitterName> ParseSplitter(std::wstring const& name)
    {
        constexpr std::wstring_view kPrefix = L"urusi-splitter:";

        if (name.rfind(kPrefix, 0) != 0 || name.size() < kPrefix.size() + 1)
        {
            return std::nullopt;
        }

        wchar_t direction = name[kPrefix.size()];
        if (direction != L'h' && direction != L'v')
        {
            return std::nullopt;
        }

        SplitterName out;
        out.horizontal = direction == L'h';

        auto rest = name.substr(kPrefix.size() + 1);
        if (!rest.empty() && rest[0] == L':')
        {
            rest.erase(0, 1);
            auto colon = rest.find(L':');
            out.before = rest.substr(0, colon);
            if (colon != std::wstring::npos)
            {
                out.after = rest.substr(colon + 1);
            }
        }
        return out;
    }

    // Which part a drag gives a size of its own to, and what size.
    struct SplitterResize
    {
        bool before{ true };
        double length{ 0 };
    };

    // Where a splitter dragged MOVED from where it was leaves the parts
    // either side, which were BEFORE and AFTER long when the drag began.
    //
    // Neither part is made shorter than MINIMUM. The part with a size of
    // its own is the one given the new size, and the one that shares
    // what is left takes the rest; if neither has one, the one before
    // is given one.
    inline SplitterResize DragSplitter(double before, double after, double moved,
                                       bool beforeShares, bool afterShares,
                                       double minimum = 40)
    {
        // Parts already shorter than that between them do not move at
        // all: there is no room to give either.
        double least = minimum - before;
        double most = after - minimum;
        moved = least > most ? 0 : std::clamp(moved, least, most);

        if (beforeShares && !afterShares)
        {
            return { false, after - moved };
        }
        return { true, before + moved };
    }
}
