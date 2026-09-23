#include <gtest/gtest.h>

#include "Input/KeyTranslation.h"

using urusi::core::input::KeyEvent;
using urusi::windows::input::TranslateKey;

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

// The key that turns the input method on and off belongs to Windows,
// and reaches the window only where the input method turned it down.
TEST(KeyTranslationTest, TheChordThatSwitchesTheInputMethodIsNotEmacs)
{
    using urusi::windows::input::IsInputMethodChord;
    urusi::core::input::KeyEvent chord{
        .key = VK_OEM_3, .scanCode = 0x29, .menuDown = true, .down = true,
    };

    EXPECT_TRUE(IsInputMethodChord(chord, true));
    // The same keys on a keyboard with no input method are Alt and a
    // character, which Emacs reads as one of its own.
    EXPECT_FALSE(IsInputMethodChord(chord, false));

    // Alt and anything else is Emacs's either way.
    urusi::core::input::KeyEvent other{
        .key = 0x58, .scanCode = 0x2D, .menuDown = true, .down = true,
    };
    EXPECT_FALSE(IsInputMethodChord(other, true));

    // And the key alone, without Alt, is the input method's own to
    // answer rather than a chord.
    urusi::core::input::KeyEvent alone{
        .key = VK_OEM_3, .scanCode = 0x29, .menuDown = false, .down = true,
    };
    EXPECT_FALSE(IsInputMethodChord(alone, true));
}
