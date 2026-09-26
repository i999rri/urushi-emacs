#pragma once

#include <winrt/Windows.Data.Json.h>

#include <optional>
#include <string>

#include "Window/DrawCommand.h"

namespace urusi::windows::emacs
{
    // Gathers the lines of a "draw" message into the screen they say.
    //
    // Emacs sends one line for each thing to draw, so that neither side
    // has to hold a whole screen as a tree before any of it is used.
    // They are gathered here, on the thread that reads them, and the
    // screen is handed over whole once its "end" arrives: half a screen
    // shown is a screen no one drew.
    class DrawReader
    {
    public:
        // Take one line, already parsed.  Returns the screen when the
        // line ends one, and nothing while one is still being read.
        std::optional<core::window::DrawFrame> Take(
            winrt::Windows::Data::Json::JsonObject const& said);

        // Why the last line was not understood, or nothing.
        std::wstring const& Why() const { return m_why; }

    private:
        core::window::DrawFrame m_frame;
        bool m_begun{ false };
        std::wstring m_why;
    };
}
