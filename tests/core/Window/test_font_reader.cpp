// Reading a `font' message where it lies.

#include <gtest/gtest.h>

#include <string>

#include "Text/Base64.h"
#include "Window/FontReader.h"

using urusi::core::text::DecodeBase64;
using urusi::core::window::FontSaid;
using urusi::core::window::ReadFont;

TEST(FontReaderTest, WhichFileAFontIs)
{
    FontSaid said;

    ASSERT_TRUE(ReadFont(
        R"({"type":"font","id":3,"instance":2,"face":1,)"
        R"("file":"/usr/share/fonts/Sarasa-Regular.ttc","size":83660396,"when":1759000000})",
        said));
    EXPECT_EQ(said.id, 3);
    EXPECT_EQ(said.instance, 2);
    EXPECT_EQ(said.face, 1);
    EXPECT_EQ(said.file, "/usr/share/fonts/Sarasa-Regular.ttc");
    EXPECT_EQ(said.size, 83660396);
    EXPECT_EQ(said.when, 1759000000);
    EXPECT_FALSE(said.hasBytes);
}

TEST(FontReaderTest, APathKeepsItsBackslashesAndQuotes)
{
    FontSaid said;

    // Emacs escapes those two and nothing else, and the name the file
    // is kept under is made of the path: one left escaped would be
    // another name, and the file would come again on every run.
    ASSERT_TRUE(ReadFont(
        R"({"type":"font","id":0,"file":"c:\\Windows\\Fonts\\a\"b.ttf","size":1,"when":2})",
        said));
    EXPECT_EQ(said.file, R"(c:\Windows\Fonts\a"b.ttf)");
}

TEST(FontReaderTest, TheFileItselfIsLeftWhereItLies)
{
    FontSaid said;
    std::string const line =
        R"({"type":"font","id":7,"bytes":"AAEAAAALAIAAAwAw"})";

    ASSERT_TRUE(ReadFont(line, said));
    EXPECT_EQ(said.id, 7);
    ASSERT_TRUE(said.hasBytes);
    EXPECT_EQ(said.bytes, "AAEAAAALAIAAAwAw");

    // A view into the line, not a copy of it.
    EXPECT_GE(said.bytes.data(), line.data());
    EXPECT_LE(said.bytes.data() + said.bytes.size(), line.data() + line.size());
}

TEST(FontReaderTest, TheBytesOfATrueTypeFileComeBackAsTheyWere)
{
    FontSaid said;

    ASSERT_TRUE(ReadFont(R"({"type":"font","id":0,"bytes":"AAEAAAALAIAAAwAw"})", said));

    auto const bytes = DecodeBase64(said.bytes);
    ASSERT_EQ(bytes.size(), 12u);
    EXPECT_EQ(bytes[0], 0x00);
    EXPECT_EQ(bytes[1], 0x01);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);
}

TEST(FontReaderTest, PaddingIsNoTrouble)
{
    // "Man" and "Ma", which pad with one = and two.
    EXPECT_EQ(DecodeBase64("TWFu").size(), 3u);
    EXPECT_EQ(DecodeBase64("TWE=").size(), 2u);
    EXPECT_EQ(DecodeBase64("TQ==").size(), 1u);
    EXPECT_TRUE(DecodeBase64("").empty());
}

TEST(FontReaderTest, AKeyThisDoesNotKnowIsPassedOver)
{
    FontSaid said;

    // Whatever a later protocol adds, the keys after it are still read.
    ASSERT_TRUE(ReadFont(
        R"({"type":"font","id":4,"whatever":"a string","also":[1,2],"more":true,)"
        R"("file":"x.ttf","size":9,"when":8})",
        said));
    EXPECT_EQ(said.id, 4);
    EXPECT_EQ(said.file, "x.ttf");
    EXPECT_EQ(said.size, 9);
}

TEST(FontReaderTest, ALineOfAnotherKindIsNoFont)
{
    FontSaid said;

    EXPECT_FALSE(ReadFont(R"({"type":"draw","op":"begin","frame":"a"})", said));
    EXPECT_FALSE(ReadFont("not a message at all", said));
    EXPECT_FALSE(ReadFont(R"({"type":"font"})", said));
}
