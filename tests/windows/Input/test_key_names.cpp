#include <gtest/gtest.h>

#include "Input/KeyNames.h"

using urushi::core::input::KeyEvent;
using urushi::windows::input::KeyForEmacs;
using urushi::windows::input::KeyModifiers;

namespace
{
    std::vector<std::string> Names(std::initializer_list<char const*> names)
    {
        return { names.begin(), names.end() };
    }

    // A character as a number, which googletest can print.
    uint32_t Code(char32_t character)
    {
        return static_cast<uint32_t>(character);
    }
}

TEST(KeyNamesTest, PrintableKeyIsItsCharacter)
{
    auto key = KeyForEmacs({ .key = 'A', .down = true }, U'a', {});
    ASSERT_TRUE(key);
    EXPECT_TRUE(key->name.empty());
    EXPECT_EQ(Code(key->character), Code(U'a'));
    EXPECT_TRUE(key->down);
    EXPECT_FALSE(key->repeat);
    EXPECT_TRUE(key->modifiers.empty());
}

// C-a is the character a with ctrl, as the layout types it without
// Ctrl, and Emacs makes the control character of it.
TEST(KeyNamesTest, ControlIsAModifierOfTheCharacter)
{
    auto key = KeyForEmacs({ .key = 'A' }, U'a', { .control = true });
    ASSERT_TRUE(key);
    EXPECT_EQ(Code(key->character), Code(U'a'));
    EXPECT_EQ(key->modifiers, Names({ "ctrl" }));
}

TEST(KeyNamesTest, AltIsMetaAndTheWindowsKeySuper)
{
    auto key = KeyForEmacs({ .key = 'X' }, U'x', { .alt = true, .windows = true });
    ASSERT_TRUE(key);
    EXPECT_EQ(key->modifiers, Names({ "meta", "super" }));

    auto all = KeyForEmacs({ .key = 'X' }, U'X',
                           { .shift = true, .control = true, .alt = true, .windows = true });
    ASSERT_TRUE(all);
    EXPECT_EQ(all->modifiers, Names({ "ctrl", "meta", "shift", "super" }));
}

// Emacs binds these by name, although the layout types a character for
// each of them.
TEST(KeyNamesTest, KeysWithControlCharactersAreNamed)
{
    std::pair<int, char const*> keys[] = {
        { VK_RETURN, "return" }, { VK_TAB, "tab" }, { VK_BACK, "backspace" },
        { VK_ESCAPE, "escape" },
    };
    for (auto [vk, name] : keys)
    {
        auto key = KeyForEmacs({ .key = vk }, U'\r', {});
        ASSERT_TRUE(key) << vk;
        EXPECT_EQ(key->name, name);
        EXPECT_EQ(Code(key->character), Code(0U));
    }
}

TEST(KeyNamesTest, KeysWithoutCharactersAreNamed)
{
    std::pair<int, char const*> keys[] = {
        { VK_DELETE, "delete" }, { VK_INSERT, "insert" }, { VK_HOME, "home" },
        { VK_END, "end" }, { VK_PRIOR, "prior" }, { VK_NEXT, "next" },
        { VK_LEFT, "left" }, { VK_RIGHT, "right" }, { VK_UP, "up" }, { VK_DOWN, "down" },
        { VK_F1, "f1" }, { VK_F12, "f12" }, { VK_F24, "f24" },
        { VK_APPS, "menu" }, { VK_PAUSE, "pause" }, { VK_SNAPSHOT, "print" },
    };
    for (auto [vk, name] : keys)
    {
        auto key = KeyForEmacs({ .key = vk }, 0, {});
        ASSERT_TRUE(key) << vk;
        EXPECT_EQ(key->name, name);
    }
}

// The keypad's keys are named as the keypad's, although they type the
// same characters as the others, as Emacs names them on any system.
TEST(KeyNamesTest, KeypadKeysAreTheKeypads)
{
    std::pair<int, char const*> keys[] = {
        { VK_NUMPAD0, "kp-0" }, { VK_NUMPAD9, "kp-9" }, { VK_ADD, "kp-add" },
        { VK_SUBTRACT, "kp-subtract" }, { VK_MULTIPLY, "kp-multiply" },
        { VK_DIVIDE, "kp-divide" }, { VK_DECIMAL, "kp-decimal" },
    };
    for (auto [vk, name] : keys)
    {
        auto key = KeyForEmacs({ .key = vk }, U'0', {});
        ASSERT_TRUE(key) << vk;
        EXPECT_EQ(key->name, name);
    }

    auto enter = KeyForEmacs({ .key = VK_RETURN, .extended = true }, U'\r', {});
    ASSERT_TRUE(enter);
    EXPECT_EQ(enter->name, "kp-enter");
}

TEST(KeyNamesTest, NamedKeysKeepTheirModifiers)
{
    auto key = KeyForEmacs({ .key = VK_LEFT }, 0, { .shift = true, .control = true });
    ASSERT_TRUE(key);
    EXPECT_EQ(key->name, "left");
    EXPECT_EQ(key->modifiers, Names({ "ctrl", "shift" }));
}

TEST(KeyNamesTest, ModifiersAloneAreNotSent)
{
    for (int vk : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                    VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN, VK_CAPITAL })
    {
        EXPECT_FALSE(KeyForEmacs({ .key = vk }, 0, { .shift = true })) << vk;
    }
}

// What the input method takes is its own, and what it makes of it
// comes as text.
TEST(KeyNamesTest, InputMethodKeysAreNotSent)
{
    for (int vk : { VK_KANJI, VK_CONVERT, VK_NONCONVERT, VK_PROCESSKEY, VK_OEM_AUTO })
    {
        EXPECT_FALSE(KeyForEmacs({ .key = vk }, U'a', {})) << vk;
    }
    EXPECT_FALSE(KeyForEmacs({ .key = 0 }, U'a', {}));
}

// A dead key types nothing yet, and an unnamed key that makes a control
// character makes nothing Emacs could type.
TEST(KeyNamesTest, KeysThatTypeNothingAreNotSent)
{
    EXPECT_FALSE(KeyForEmacs({ .key = VK_OEM_7 }, 0, {}));
    EXPECT_FALSE(KeyForEmacs({ .key = 'A' }, 0x01, { .control = true }));
    EXPECT_FALSE(KeyForEmacs({ .key = VK_OEM_4 }, 0x7F, {}));
}

TEST(KeyNamesTest, CharactersPastTheFirstPlaneArePassedWhole)
{
    auto key = KeyForEmacs({ .key = 'A' }, U'\U0001F600', {});
    ASSERT_TRUE(key);
    EXPECT_EQ(Code(key->character), Code(U'\U0001F600'));
}

TEST(KeyNamesTest, ReleaseAndRepeatAreSaid)
{
    auto up = KeyForEmacs({ .key = 'A', .wasDown = true, .down = false }, U'a', {});
    ASSERT_TRUE(up);
    EXPECT_FALSE(up->down);
    EXPECT_FALSE(up->repeat);

    auto held = KeyForEmacs({ .key = 'A', .wasDown = true, .down = true }, U'a', {});
    ASSERT_TRUE(held);
    EXPECT_TRUE(held->repeat);
}
