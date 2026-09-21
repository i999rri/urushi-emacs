#include <gtest/gtest.h>

#include "Input/ImeBuffer.h"

using urusi::core::input::ImeBuffer;

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

// The focus went and came back, and the buffer started again from
// nothing, but the input method did not: it goes on at 76, and when the
// window goes again mid-composition it replaces what it composed with
// the same text. That is a replacement, not more text.
TEST(ImeBufferTest, CountIsTakenFromTheInputMethod)
{
    ImeBuffer buffer;
    buffer.Reset();
    buffer.Started();
    buffer.Update(76, 76, L"あ");
    buffer.Update(77, 77, L"い");
    EXPECT_EQ(buffer.Composed(), L"あい");

    buffer.Update(76, 78, L"あい");
    EXPECT_EQ(buffer.Composed(), L"あい");
    EXPECT_EQ(buffer.Settle(), L"あい");
    EXPECT_EQ(buffer.Caret(), 78);
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
