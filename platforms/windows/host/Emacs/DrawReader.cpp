#include "pch.h"

#include <algorithm>
#include "Emacs/DrawReader.h"

using namespace winrt;
using namespace winrt::Windows::Data::Json;
using urusi::core::window::DrawCommand;
using urusi::core::window::DrawFrame;
using urusi::core::window::DrawOp;

namespace
{
    // "#rrggbb" as Emacs writes a color, or black.
    uint32_t ColorOf(JsonObject const& said)
    {
        auto text = said.GetNamedString(L"color", L"");
        if (text.size() != 7 || text[0] != L'#')
        {
            return 0;
        }
        return static_cast<uint32_t>(std::wcstoul(text.c_str() + 1, nullptr, 16));
    }

    int NumberOf(JsonObject const& said, wchar_t const* name)
    {
        return static_cast<int>(said.GetNamedNumber(name, 0));
    }

    void ReadGlyphs(JsonObject const& said, DrawCommand& command)
    {
        command.font = NumberOf(said, L"font");
        command.size = said.GetNamedNumber(L"size", 0);
        command.y = NumberOf(said, L"y");
        command.color = ColorOf(said);

        auto ids = said.GetNamedArray(L"ids", nullptr);
        auto xs = said.GetNamedArray(L"xs", nullptr);
        if (!ids || !xs)
        {
            return;
        }

        auto count = (std::min)(ids.Size(), xs.Size());
        command.ids.reserve(count);
        command.xs.reserve(count);
        for (uint32_t at = 0; at < count; ++at)
        {
            command.ids.push_back(static_cast<uint16_t>(ids.GetNumberAt(at)));
            command.xs.push_back(static_cast<int>(xs.GetNumberAt(at)));
        }
    }
}

namespace urusi::windows::emacs
{
    std::optional<DrawFrame> DrawReader::Take(JsonObject const& said)
    {
        m_why.clear();

        auto op = said.GetNamedString(L"op", L"");

        if (op == L"begin")
        {
            m_frame = DrawFrame{};
            m_frame.frame = said.GetNamedString(L"frame", L"");
            m_frame.width = NumberOf(said, L"width");
            m_frame.height = NumberOf(said, L"height");
            m_begun = true;
            return std::nullopt;
        }

        // A line before any beginning is one whose beginning was lost,
        // and drawing from it would draw onto the screen before it.
        if (!m_begun)
        {
            m_why = L"a line of a screen that never began: " + std::wstring{ op };
            return std::nullopt;
        }

        if (op == L"end")
        {
            m_begun = false;
            return std::move(m_frame);
        }

        DrawCommand command;
        if (op == L"fill" || op == L"rectangle" || op == L"clip")
        {
            command.op = op == L"fill"        ? DrawOp::Fill
                         : op == L"rectangle" ? DrawOp::Rectangle
                                              : DrawOp::Clip;
            command.x = NumberOf(said, L"x");
            command.y = NumberOf(said, L"y");
            command.width = NumberOf(said, L"width");
            command.height = NumberOf(said, L"height");
            command.color = ColorOf(said);
        }
        else if (op == L"line")
        {
            command.op = DrawOp::Line;
            command.x = NumberOf(said, L"x0");
            command.y = NumberOf(said, L"y0");
            command.width = NumberOf(said, L"x1");
            command.height = NumberOf(said, L"y1");
            command.color = ColorOf(said);
        }
        else if (op == L"copy")
        {
            command.op = DrawOp::Copy;
            command.x = NumberOf(said, L"x");
            command.y = NumberOf(said, L"y");
            command.width = NumberOf(said, L"width");
            command.height = NumberOf(said, L"height");
            command.toY = NumberOf(said, L"toY");
        }
        else if (op == L"unclip")
        {
            command.op = DrawOp::Unclip;
        }
        else if (op == L"glyphs")
        {
            command.op = DrawOp::Glyphs;
            ReadGlyphs(said, command);
        }
        else
        {
            m_why = L"a kind of drawing this host does not know: " + std::wstring{ op };
            return std::nullopt;
        }

        m_frame.commands.push_back(std::move(command));
        return std::nullopt;
    }
}
