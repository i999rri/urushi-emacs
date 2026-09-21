#include <gtest/gtest.h>

#include "Rows.h"

#include <string>
#include <vector>

using urusi::Arrange;
using urusi::PlanRows;
using urusi::RowItem;

namespace
{
    // A panel's children, counting what is done to them.
    struct Children
    {
        std::vector<std::string> items;
        int moves{ 0 };

        uint32_t Size() const { return static_cast<uint32_t>(items.size()); }
        std::string At(uint32_t i) const { return items[i]; }

        bool IndexOf(std::string const& x, uint32_t& at) const
        {
            for (uint32_t i = 0; i < items.size(); ++i)
            {
                if (items[i] == x)
                {
                    at = i;
                    return true;
                }
            }
            return false;
        }

        void RemoveAt(uint32_t i) { items.erase(items.begin() + i); ++moves; }
        void InsertAt(uint32_t i, std::string const& x) { items.insert(items.begin() + i, x); ++moves; }
        void RemoveAtEnd() { items.pop_back(); ++moves; }
    };
}

TEST(RowsTest, RowsThatComeWithXamlAreBuiltAndTheRestKept)
{
    auto plan = PlanRows({ L"a", L"b", L"c" },
                         { { L"a", false }, { L"x", true }, { L"c", false } });
    ASSERT_FALSE(plan.stale);
    ASSERT_EQ(plan.sources.size(), 3u);
    EXPECT_EQ(plan.sources[0].kept, 0u);
    EXPECT_FALSE(plan.sources[1].kept);
    EXPECT_EQ(plan.sources[2].kept, 2u);
}

// Emacs thinks the panel shows a row it does not: the whole screen is to
// be asked for again, and nothing is changed meanwhile.
TEST(RowsTest, ARowThatIsNotThereMakesTheScreenStale)
{
    auto plan = PlanRows({ L"a" }, { { L"a", false }, { L"b", false } });
    EXPECT_TRUE(plan.stale);
    EXPECT_TRUE(plan.sources.empty());
}

TEST(RowsTest, NothingMovesWhenNothingChanged)
{
    Children children{ { "a", "b", "c" } };
    Arrange(children, std::vector<std::string>{ "a", "b", "c" });
    EXPECT_EQ(children.items, (std::vector<std::string>{ "a", "b", "c" }));
    EXPECT_EQ(children.moves, 0);
}


TEST(RowsTest, ANewRowGoesWhereItIsWanted)
{
    Children children{ { "a", "c" } };
    Arrange(children, std::vector<std::string>{ "a", "b", "c" });
    EXPECT_EQ(children.items, (std::vector<std::string>{ "a", "b", "c" }));
    EXPECT_EQ(children.moves, 1);
}

TEST(RowsTest, WhatIsNotWantedIsTakenAway)
{
    Children children{ { "a", "b", "c", "d" } };
    Arrange(children, std::vector<std::string>{ "c", "a" });
    EXPECT_EQ(children.items, (std::vector<std::string>{ "c", "a" }));
}

