#include <gtest/gtest.h>

#include "Input/ImeBuffer.h"

using urushi::core::input::ImeBuffer;

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

// What was settled on is Emacs's, and the input method begins the next
// composition from nothing: the positions are of the composition and
// not of everything that has been typed.
TEST(ImeBufferTest, NextCompositionBeginsFromNothing)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"かん");
    EXPECT_EQ(buffer.Settle(), L"かん");

    buffer.Started();
    buffer.Update(0, 0, L"じ");
    EXPECT_EQ(buffer.Composed(), L"じ");
    buffer.Update(0, 1, L"字");
    EXPECT_EQ(buffer.Settle(), L"字");
}

// A key typed with the input method open and idle comes as text with no
// composition, and is settled the same way.
TEST(ImeBufferTest, TextWithoutCompositionIsSettledToo)
{
    ImeBuffer buffer;
    buffer.Update(0, 0, L"　");
    EXPECT_FALSE(buffer.Composing());
    EXPECT_EQ(buffer.Settle(), L"　");
}

// Asked to replace more than there is, the buffer takes what there is:
// a position it has not reached is the end of what it holds.
TEST(ImeBufferTest, PositionsPastTheEndAreTakenAsTheEnd)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"あい");

    buffer.Update(1, 100, L"う");
    EXPECT_EQ(buffer.Composed(), L"あう");
}

TEST(ImeBufferTest, ResetDropsWhatWasBeingComposed)
{
    ImeBuffer buffer;
    buffer.Started();
    buffer.Update(0, 0, L"じ");

    buffer.Reset();
    EXPECT_FALSE(buffer.Composing());
    EXPECT_EQ(buffer.Composed(), L"");
}
