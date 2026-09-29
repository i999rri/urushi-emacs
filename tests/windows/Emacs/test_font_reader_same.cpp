// That the reader written by hand reads the font the one it replaced
// read, and what it saves on a file of the size that really arrives.
//
// The host used to take a `font' message through Windows.Data.Json and
// its base64 through CryptographicBuffer.  Both want a string of the
// platform's own, so the line went over again as UTF-16: a font file of
// eighty megabytes is a hundred and ten megabytes of base64 and two
// hundred and twenty as UTF-16, twice, on the thread that reads from
// Emacs -- which reads nothing else while it is busy, so the window
// stops for as long.
//
// Fonts only come that way where Emacs is a process of its own: loaded
// into the application it says the path and the application opens it.
// So this is the remote path's cost, and it is what froze the window
// with the Emacs for Linux in WSL.
//
// Nothing here is in the application: the reader as it was is kept only
// to hold the reader as it is to it.

#include <gtest/gtest.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "Measured.h"
#include "Text/Base64.h"
#include "Window/FontReader.h"

using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Security::Cryptography::CryptographicBuffer;
using urusi::core::text::DecodeBase64;
using urusi::core::window::FontSaid;
using urusi::core::window::ReadFont;
using urusi::tests::kChecked;
using urusi::tests::Measured;

namespace
{
    // The reader as it was: a tree of the whole line, and the base64
    // through the platform.
    struct Parsed
    {
        int id{ -1 };
        std::wstring file;
        int instance{ -1 };
        int face{ 0 };
        int64_t size{ 0 };
        int64_t when{ 0 };
        bool hasBytes{ false };
        std::vector<uint8_t> bytes;
    };

    bool ParseFont(std::string const& line, Parsed& into)
    {
        JsonObject said{ nullptr };

        if (!JsonObject::TryParse(winrt::to_hstring(line), said))
        {
            return false;
        }
        into.id = static_cast<int>(said.GetNamedNumber(L"id", -1));
        into.file = std::wstring{ said.GetNamedString(L"file", L"") };
        into.instance = static_cast<int>(said.GetNamedNumber(L"instance", -1));
        into.face = static_cast<int>(said.GetNamedNumber(L"face", 0));
        into.size = static_cast<int64_t>(said.GetNamedNumber(L"size", 0));
        into.when = static_cast<int64_t>(said.GetNamedNumber(L"when", 0));
        into.hasBytes = said.HasKey(L"bytes");
        if (into.hasBytes)
        {
            auto buffer = CryptographicBuffer::DecodeFromBase64String(
                said.GetNamedString(L"bytes", L""));
            into.bytes.assign(buffer.data(), buffer.data() + buffer.Length());
        }
        return true;
    }

    std::string Base64Of(std::vector<uint8_t> const& bytes)
    {
        char const* const alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string text;

        text.reserve((bytes.size() + 2) / 3 * 4);
        for (size_t at = 0; at < bytes.size(); at += 3)
        {
            uint32_t held = static_cast<uint32_t>(bytes[at]) << 16;
            size_t const left = bytes.size() - at;

            if (left > 1) { held |= static_cast<uint32_t>(bytes[at + 1]) << 8; }
            if (left > 2) { held |= static_cast<uint32_t>(bytes[at + 2]); }

            text.push_back(alphabet[(held >> 18) & 0x3F]);
            text.push_back(alphabet[(held >> 12) & 0x3F]);
            text.push_back(left > 1 ? alphabet[(held >> 6) & 0x3F] : '=');
            text.push_back(left > 2 ? alphabet[held & 0x3F] : '=');
        }
        return text;
    }

    // Bytes that are not all alike, so that a decoder that lost its
    // place would say so.
    std::vector<uint8_t> SomeBytes(size_t many)
    {
        std::vector<uint8_t> bytes(many);

        for (size_t at = 0; at < many; ++at)
        {
            bytes[at] = static_cast<uint8_t>((at * 31 + (at >> 8) * 7) & 0xFF);
        }
        return bytes;
    }

    std::string FontLine(int id, std::vector<uint8_t> const& bytes)
    {
        return R"({"type":"font","id":)" + std::to_string(id)
            + R"(,"bytes":")" + Base64Of(bytes) + R"("})";
    }
}

TEST(FontReaderSameTest, BothReadersMakeTheSameOfWhichFileAFontIs)
{
    std::string const line =
        R"({"type":"font","id":3,"instance":2,"face":1,)"
        R"("file":"c:\\Users\\a\\Iosevka.ttf","size":13107200,"when":1759000000})";

    FontSaid ours;
    Parsed theirs;

    ASSERT_TRUE(ReadFont(line, ours));
    ASSERT_TRUE(ParseFont(line, theirs));

    EXPECT_EQ(ours.id, theirs.id);
    EXPECT_EQ(ours.instance, theirs.instance);
    EXPECT_EQ(ours.face, theirs.face);
    EXPECT_EQ(ours.size, theirs.size);
    EXPECT_EQ(ours.when, theirs.when);
    EXPECT_EQ(ours.hasBytes, theirs.hasBytes);
    // The path is what the name a file is kept under is made of, so a
    // difference of one character here would fetch every font again.
    EXPECT_EQ(winrt::to_hstring(ours.file), winrt::hstring{ theirs.file });
}

TEST(FontReaderSameTest, BothReadersMakeTheSameBytesOfAFile)
{
    auto const bytes = SomeBytes(64 * 1024 + 7);
    auto const line = FontLine(11, bytes);

    FontSaid ours;
    Parsed theirs;

    ASSERT_TRUE(ReadFont(line, ours));
    ASSERT_TRUE(ParseFont(line, theirs));
    ASSERT_TRUE(ours.hasBytes);
    ASSERT_TRUE(theirs.hasBytes);

    auto const mine = DecodeBase64(ours.bytes);
    EXPECT_EQ(mine, bytes);
    EXPECT_EQ(mine, theirs.bytes);
}

TEST(FontReaderSameTest, TheHandWrittenReaderIsTheFasterOfTheTwo)
{
    if (kChecked)
    {
        SUCCEED() << "not timed: the checked standard library is not what"
                     " either of these runs on";
        return;
    }

    // The size a font really is: Iosevka Nerd Font Mono is twelve and a
    // half megabytes, and a CJK collection six times that.  Twelve is
    // taken here so that a test run stays short.
    auto const bytes = SomeBytes(12 * 1024 * 1024);
    auto const line = FontLine(0, bytes);

    auto const began = std::chrono::steady_clock::now();
    FontSaid ours;
    ASSERT_TRUE(ReadFont(line, ours));
    auto const mine = DecodeBase64(ours.bytes);
    auto const readByHand = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - began).count();

    auto const then = std::chrono::steady_clock::now();
    Parsed theirs;
    ASSERT_TRUE(ParseFont(line, theirs));
    auto const readIntoObjects = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - then).count();

    ASSERT_EQ(mine, bytes);
    ASSERT_EQ(theirs.bytes, bytes);

    Measured("font.read.by.hand", readByHand, "milliseconds");
    Measured("font.read.into.objects", readIntoObjects, "milliseconds");
    Measured("font.read.times.faster",
             readIntoObjects / (readByHand > 0 ? readByHand : 1), "times");

    EXPECT_LT(readByHand, readIntoObjects);
}
