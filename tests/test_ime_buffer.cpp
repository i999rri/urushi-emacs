#include <gtest/gtest.h>

#include "ImeBuffer.h"

using urusi::ImeBuffer;

TEST(ImeBufferTest, CompositionGrowsAndIsSettledOnce)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"k");
    buffer.Update(0, 1, L"か");
    buffer.Update(1, 1, L"n");
    buffer.Update(1, 2, L"ん");
    EXPECT_TRUE(buffer.Composing());
    EXPECT_EQ(buffer.Composed(), L"かん");

    EXPECT_EQ(buffer.Settle(), L"かん");
    EXPECT_FALSE(buffer.Composing());
    EXPECT_EQ(buffer.Composed(), L"");
}

// The input method counts on from where the last composition ended,
// and the next one arrives at those positions.
TEST(ImeBufferTest, NextCompositionIsCountedFromWhereTheLastEnded)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"かん");
    buffer.Settle();
    EXPECT_EQ(buffer.Caret(), 2);

    buffer.Started();
    buffer.Update(2, 2, L"じ");
    EXPECT_EQ(buffer.Composed(), L"じ");
    buffer.Update(2, 3, L"字");
    EXPECT_EQ(buffer.Settle(), L"字");
    EXPECT_EQ(buffer.Caret(), 3);
}

TEST(ImeBufferTest, TextHandedOnIsAnsweredWithSpaces)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"かん");
    buffer.Settle();
    buffer.Started();
    buffer.Update(2, 2, L"じ");

    EXPECT_EQ(buffer.Text(0, 3), L"  じ");
    EXPECT_EQ(buffer.Text(2, 3), L"じ");
    // Asked past the end, answered up to it.
    EXPECT_EQ(buffer.Text(1, 100), L" じ");
}

// A key typed with the input method open and idle comes as text with
// no composition, and is counted like one.
TEST(ImeBufferTest, TextWithoutCompositionIsCountedToo)
{
    ImeBuffer buffer;
    buffer.Update(0, 0, L"　");
    EXPECT_FALSE(buffer.Composing());
    EXPECT_EQ(buffer.Settle(), L"　");
    EXPECT_EQ(buffer.Caret(), 1);

    buffer.Started();
    buffer.Update(1, 1, L"あ");
    EXPECT_EQ(buffer.Composed(), L"あ");
}

TEST(ImeBufferTest, ResetCountsFromNothing)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"かん");
    buffer.Settle();
    buffer.Started();
    buffer.Update(2, 2, L"じ");

    buffer.Reset();
    EXPECT_FALSE(buffer.Composing());
    EXPECT_EQ(buffer.Caret(), 0);
    EXPECT_EQ(buffer.Text(0, 10), L"");
}
