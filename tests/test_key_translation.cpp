#include <gtest/gtest.h>

#include "KeyTranslation.h"

using urusi::KeyEvent;
using urusi::TranslateKey;

TEST(KeyTranslationTest, KeyIsTheMessageWindowsWouldHaveSent)
{
    auto message = TranslateKey({ .key = 'A', .repeat = 1, .scanCode = 0x1E, .down = true });
    ASSERT_TRUE(message);
    EXPECT_EQ(message->message, static_cast<UINT>(WM_KEYDOWN));
    EXPECT_EQ(message->wParam, static_cast<WPARAM>('A'));
    // Repeat count 1, scan code in bits 16-23, nothing else.
    EXPECT_EQ(message->lParam, static_cast<LPARAM>(0x001E0001));
}

TEST(KeyTranslationTest, ReleaseSaysItWasDownAndIsGoingUp)
{
    auto message = TranslateKey({ .key = 'A', .scanCode = 0x1E, .wasDown = true, .down = false });
    ASSERT_TRUE(message);
    EXPECT_EQ(message->message, static_cast<UINT>(WM_KEYUP));
    EXPECT_NE(message->lParam & (LPARAM{ 1 } << 30), 0);
    EXPECT_NE(message->lParam & (LPARAM{ 1 } << 31), 0);
}

// Alt held is the meta key to Emacs, and Windows calls such a key a
// system key.
TEST(KeyTranslationTest, AltHeldIsASystemKey)
{
    auto down = TranslateKey({ .key = 'X', .menuDown = true, .down = true });
    auto up = TranslateKey({ .key = 'X', .menuDown = true, .down = false });
    ASSERT_TRUE(down && up);
    EXPECT_EQ(down->message, static_cast<UINT>(WM_SYSKEYDOWN));
    EXPECT_EQ(up->message, static_cast<UINT>(WM_SYSKEYUP));
    EXPECT_NE(down->lParam & (LPARAM{ 1 } << 29), 0);
}

TEST(KeyTranslationTest, ExtendedKeysSaySo)
{
    auto message = TranslateKey({ .key = VK_RIGHT, .extended = true });
    ASSERT_TRUE(message);
    EXPECT_NE(message->lParam & (LPARAM{ 1 } << 24), 0);
}

// The keys that work the input method, and the key that stands for one
// it has already taken, are not Emacs's: passed on, they would switch
// nothing, or type what the input method types as well.
TEST(KeyTranslationTest, InputMethodKeysAreNotPassedOn)
{
    for (int key : { VK_KANJI, VK_CONVERT, VK_NONCONVERT, VK_PROCESSKEY, VK_OEM_AUTO,
                     VK_OEM_ENLW, VK_KANA })
    {
        EXPECT_FALSE(TranslateKey({ .key = key })) << key;
    }
    EXPECT_FALSE(TranslateKey({ .key = 0 }));
}
