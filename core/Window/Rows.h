#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace urusi::core::window
{
    // A row of a screen as Lisp sends it: its key, and whether it comes
    // with XAML of its own to be built, or is one the host already has.
    struct RowItem
    {
        std::wstring key;
        bool built{ false };
    };

    // For each row, where it comes from: built from its XAML, or kept,
    // the one at this place among what the panel has now.
    struct RowSource
    {
        std::optional<uint32_t> kept;
    };

    // What to do with the rows of a panel. STALE is set when a row that
    // is to be kept is not there: Emacs thinks the panel shows something
    // it does not, and the whole screen is to be asked for again.
    struct RowPlan
    {
        bool stale{ false };
        std::vector<RowSource> sources;
    };

    // Decide where each of ITEMS comes from, given the keys of the rows
    // the panel has now, in their order.
    inline RowPlan PlanRows(std::vector<std::wstring> const& existing,
                            std::vector<RowItem> const& items)
    {
        RowPlan plan;
        plan.sources.reserve(items.size());

        for (auto const& item : items)
        {
            if (item.built)
            {
                plan.sources.push_back({});
                continue;
            }

            std::optional<uint32_t> found;
            for (uint32_t i = 0; i < existing.size(); ++i)
            {
                if (existing[i] == item.key)
                {
                    found = i;
                    break;
                }
            }
            if (!found)
            {
                plan.stale = true;
                plan.sources.clear();
                return plan;
            }
            plan.sources.push_back({ found });
        }
        return plan;
    }

    // Put CHILDREN in the order of WANTED, moving only what is out of
    // place rather than taking the lot apart: a row that is only further
    // down the screen than it was keeps whatever it was doing.
    //
    // CHILDREN is a list of the panel's children, or anything with the
    // same few calls: Size, At, IndexOf, RemoveAt, InsertAt, RemoveAtEnd.
    template <typename List, typename Item>
    void Arrange(List& children, std::vector<Item> const& wanted)
    {
        // What is not wanted goes first. Left in, a row gone from the
        // top would stand in the place of the one after it, and every
        // row below would be taken out and put back to get past it.
        for (uint32_t i = children.Size(); i-- > 0;)
        {
            auto child = children.At(i);
            bool keep = false;
            for (auto const& item : wanted)
            {
                if (item == child)
                {
                    keep = true;
                    break;
                }
            }
            if (!keep)
            {
                children.RemoveAt(i);
            }
        }

        for (uint32_t i = 0; i < wanted.size(); ++i)
        {
            if (i < children.Size() && children.At(i) == wanted[i])
            {
                continue;
            }

            uint32_t found = 0;
            if (children.IndexOf(wanted[i], found))
            {
                children.RemoveAt(found);
            }
            children.InsertAt(i, wanted[i]);
        }

        while (children.Size() > wanted.size())
        {
            children.RemoveAtEnd();
        }
    }
}
