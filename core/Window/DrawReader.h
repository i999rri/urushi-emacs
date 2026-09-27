#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Window/DrawCommand.h"

namespace urusi::core::window
{
    namespace draw
    {
        // What one line of a screen says, whatever order it says it in.
        struct Said
        {
            std::string_view op;
            std::string_view frame;
            int x{};
            int y{};
            int x0{};
            int y0{};
            int x1{};
            int y1{};
            int width{};
            int height{};
            int toY{};
            int font{};
            int image{};
            int fromX{};
            int fromY{};
            uint32_t color{};
            double size{};
            std::vector<uint16_t> ids;
            std::vector<int> xs;
        };

        inline void PassSpace(std::string_view line, size_t& at)
        {
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
            {
                ++at;
            }
        }

        // The text of the string that begins at AT, which is its quote.
        inline std::string_view Text(std::string_view line, size_t& at)
        {
            if (at >= line.size() || line[at] != '"')
            {
                return {};
            }

            auto const from = ++at;
            while (at < line.size() && line[at] != '"')
            {
                // Nothing Emacs writes here has an escape in it, but
                // one would otherwise end the string early.
                if (line[at] == '\\' && at + 1 < line.size())
                {
                    ++at;
                }
                ++at;
            }

            auto const text = line.substr(from, at - from);
            if (at < line.size())
            {
                ++at;
            }
            return text;
        }

        inline long long Number(std::string_view line, size_t& at)
        {
            bool const below = at < line.size() && line[at] == '-';
            long long value = 0;

            if (below)
            {
                ++at;
            }
            while (at < line.size() && line[at] >= '0' && line[at] <= '9')
            {
                value = value * 10 + (line[at++] - '0');
            }
            // A fraction, which only the size of a font has.
            if (at < line.size() && line[at] == '.')
            {
                ++at;
                while (at < line.size() && line[at] >= '0' && line[at] <= '9')
                {
                    ++at;
                }
            }
            return below ? -value : value;
        }

        inline double Fraction(std::string_view line, size_t& at)
        {
            auto const from = at;

            Number(line, at);
            return std::strtod(
                std::string{ line.substr(from, at - from) }.c_str(), nullptr);
        }

        // "#rrggbb" as Emacs writes a color.
        inline uint32_t Color(std::string_view text)
        {
            uint32_t value = 0;

            if (text.size() != 7 || text[0] != '#')
            {
                return 0;
            }
            for (size_t at = 1; at < text.size(); ++at)
            {
                char const digit = text[at];
                uint32_t part;

                if (digit >= '0' && digit <= '9')
                {
                    part = digit - '0';
                }
                else if (digit >= 'a' && digit <= 'f')
                {
                    part = digit - 'a' + 10;
                }
                else if (digit >= 'A' && digit <= 'F')
                {
                    part = digit - 'A' + 10;
                }
                else
                {
                    return 0;
                }
                value = value * 16 + part;
            }
            return value;
        }

        // How many numbers the array beginning at AT holds, counted
        // without reading them: the room for them is then taken once
        // rather than a piece at a time as they arrive.
        inline size_t Many(std::string_view line, size_t at)
        {
            size_t commas = 0;
            bool any = false;

            if (at >= line.size() || line[at] != '[')
            {
                return 0;
            }
            for (++at; at < line.size() && line[at] != ']'; ++at)
            {
                if (line[at] == ',')
                {
                    ++commas;
                }
                else if (line[at] != ' ' && line[at] != '\t')
                {
                    any = true;
                }
            }
            return any ? commas + 1 : 0;
        }

        template <typename T>
        void Numbers(std::string_view line, size_t& at, std::vector<T>& into)
        {
            if (at >= line.size() || line[at] != '[')
            {
                return;
            }
            into.reserve(into.size() + Many(line, at));
            ++at;

            while (at < line.size() && line[at] != ']')
            {
                PassSpace(line, at);
                into.push_back(static_cast<T>(Number(line, at)));
                PassSpace(line, at);
                if (at < line.size() && line[at] == ',')
                {
                    ++at;
                }
            }
            if (at < line.size())
            {
                ++at;
            }
        }

        // Read one line into what it says.  It is one object of the few
        // shapes hostrecord.c writes, and nothing else is looked for.
        inline bool Read(std::string_view line, Said& said)
        {
            size_t at = line.find('{');

            if (at == std::string_view::npos)
            {
                return false;
            }
            ++at;

            while (at < line.size())
            {
                PassSpace(line, at);
                if (at >= line.size() || line[at] == '}')
                {
                    return true;
                }
                if (line[at] == ',')
                {
                    ++at;
                    continue;
                }

                auto const key = Text(line, at);
                PassSpace(line, at);
                if (at >= line.size() || line[at] != ':')
                {
                    return false;
                }
                ++at;
                PassSpace(line, at);

                if (key == "op")
                {
                    said.op = Text(line, at);
                }
                else if (key == "frame")
                {
                    said.frame = Text(line, at);
                }
                else if (key == "color")
                {
                    said.color = Color(Text(line, at));
                }
                else if (key == "ids")
                {
                    Numbers(line, at, said.ids);
                }
                else if (key == "xs")
                {
                    Numbers(line, at, said.xs);
                }
                else if (key == "size")
                {
                    said.size = Fraction(line, at);
                }
                else if (key == "x")
                {
                    said.x = static_cast<int>(Number(line, at));
                }
                else if (key == "y")
                {
                    said.y = static_cast<int>(Number(line, at));
                }
                else if (key == "x0")
                {
                    said.x0 = static_cast<int>(Number(line, at));
                }
                else if (key == "y0")
                {
                    said.y0 = static_cast<int>(Number(line, at));
                }
                else if (key == "x1")
                {
                    said.x1 = static_cast<int>(Number(line, at));
                }
                else if (key == "y1")
                {
                    said.y1 = static_cast<int>(Number(line, at));
                }
                else if (key == "width")
                {
                    said.width = static_cast<int>(Number(line, at));
                }
                else if (key == "height")
                {
                    said.height = static_cast<int>(Number(line, at));
                }
                else if (key == "toY")
                {
                    said.toY = static_cast<int>(Number(line, at));
                }
                else if (key == "font")
                {
                    said.font = static_cast<int>(Number(line, at));
                }
                else if (key == "image")
                {
                    said.image = static_cast<int>(Number(line, at));
                }
                else if (key == "fromX")
                {
                    said.fromX = static_cast<int>(Number(line, at));
                }
                else if (key == "fromY")
                {
                    said.fromY = static_cast<int>(Number(line, at));
                }
                else
                {
                    // A field this host has no use for: the value is a
                    // string, a number or an array, and all three end
                    // where the next field begins.
                    if (line[at] == '"')
                    {
                        Text(line, at);
                    }
                    else if (line[at] == '[')
                    {
                        std::vector<int> ignored;
                        Numbers(line, at, ignored);
                    }
                    else
                    {
                        Number(line, at);
                    }
                }
            }
            return true;
        }
    }

    // Gathers the lines of a "draw" message into the screen they say.
    //
    // Emacs sends one line for each thing to draw, so that neither side
    // has to hold a whole screen as a tree before any of it is used.
    // They are gathered here, on the thread that reads them, and the
    // screen is handed over whole once its "end" arrives: half a screen
    // shown is a screen no one drew.
    //
    // The lines are read as they come rather than parsed into objects
    // of their own, which is where this began: it was written after
    // measuring the one that did.
    //
    // That one read each line with Windows.Data.Json, which builds an
    // object for every value in it -- every number of a run of glyphs
    // among them.  While the window scrolled, the thread reading from
    // Emacs spent nine per cent of a processor, as much as the drawing
    // of the screen did; and a screen of four hundred and fifty
    // commands took it five milliseconds against this one's hundred
    // and thirty microseconds, which is forty times.  One glyph of a
    // long run cost it fifteen hundred nanoseconds against twenty:
    // that is the object each number was made into, and it is the
    // whole of the difference.
    //
    // Both readers are kept, and both are held to the same screen and
    // timed against each other, in tests/windows/Emacs/
    // test_draw_reader_same.cpp; what this one takes from the heap is
    // counted in tests/core/Window/test_draw_reader_cost.cpp.
    class DrawReader
    {
    public:
        // Take one line, which is one thing to draw.  Returns the
        // screen when the line ends one, and nothing while one is
        // still being read.
        std::optional<DrawFrame> Take(std::string_view line)
        {
            draw::Said said;

            m_why.clear();
            if (!draw::Read(line, said))
            {
                m_why = "a line that is no drawing";
                return std::nullopt;
            }

            if (said.op == "begin")
            {
                m_frame = DrawFrame{};
                m_frame.frame.assign(said.frame.begin(), said.frame.end());
                m_frame.width = said.width;
                m_frame.height = said.height;
                m_begun = true;
                return std::nullopt;
            }

            // A line before any beginning is one whose beginning was
            // lost, and drawing from it would draw onto the screen
            // before it.
            if (!m_begun)
            {
                m_why = "a line of a screen that never began";
                return std::nullopt;
            }

            if (said.op == "end")
            {
                m_begun = false;
                return std::move(m_frame);
            }

            DrawCommand command;
            if (said.op == "fill" || said.op == "rectangle" || said.op == "clip")
            {
                command.op = said.op == "fill"        ? DrawOp::Fill
                             : said.op == "rectangle" ? DrawOp::Rectangle
                                                      : DrawOp::Clip;
                command.x = said.x;
                command.y = said.y;
                command.width = said.width;
                command.height = said.height;
                command.color = said.color;
            }
            else if (said.op == "line")
            {
                command.op = DrawOp::Line;
                command.x = said.x0;
                command.y = said.y0;
                command.width = said.x1;
                command.height = said.y1;
                command.color = said.color;
            }
            else if (said.op == "copy")
            {
                command.op = DrawOp::Copy;
                command.x = said.x;
                command.y = said.y;
                command.width = said.width;
                command.height = said.height;
                command.toY = said.toY;
            }
            else if (said.op == "unclip")
            {
                command.op = DrawOp::Unclip;
            }
            else if (said.op == "image")
            {
                command.op = DrawOp::Image;
                command.image = said.image;
                command.fromX = said.fromX;
                command.fromY = said.fromY;
                command.x = said.x;
                command.y = said.y;
                command.width = said.width;
                command.height = said.height;
            }
            else if (said.op == "glyphs")
            {
                command.op = DrawOp::Glyphs;
                command.font = said.font;
                command.size = said.size;
                command.y = said.y;
                command.color = said.color;
                command.ids = std::move(said.ids);
                command.xs = std::move(said.xs);
                // One of each: a glyph with no place, or a place with
                // no glyph, is neither.
                auto const both = (std::min)(command.ids.size(),
                                             command.xs.size());
                command.ids.resize(both);
                command.xs.resize(both);
            }
            else
            {
                m_why = "a kind of drawing this host does not know";
                return std::nullopt;
            }

            m_frame.commands.push_back(std::move(command));
            return std::nullopt;
        }

        // Take a message, which is one line or many, and return the
        // screens its lines finished.
        //
        // Emacs writes a whole screen's lines in one go, so that a
        // screen is one write rather than a few hundred, and how they
        // arrive is the transport's doing: an Emacs of its own sends
        // them down a pipe, where they come back a line at a time, and
        // one loaded into this process posts them as they were written,
        // all in the one message.  Neither is the reader's business.
        std::vector<DrawFrame> TakeLines(std::string_view text)
        {
            std::vector<DrawFrame> screens;

            for (size_t at = 0; at < text.size();)
            {
                auto const end = text.find('\n', at);
                auto line = text.substr(at, end == std::string_view::npos
                                                ? std::string_view::npos
                                                : end - at);

                at = end == std::string_view::npos ? text.size() : end + 1;

                // As the pipe leaves them, where the line ends with a
                // carriage return that nothing else wants.
                if (!line.empty() && line.back() == '\r')
                {
                    line.remove_suffix(1);
                }
                if (line.empty())
                {
                    continue;
                }

                if (auto screen = Take(line))
                {
                    screens.push_back(std::move(*screen));
                }
            }
            return screens;
        }

        // Why the last line was not understood, or nothing.
        std::string const& Why() const { return m_why; }

    private:
        DrawFrame m_frame;
        bool m_begun{ false };
        std::string m_why;
    };
}
