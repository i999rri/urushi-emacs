#pragma once

#include <optional>
#include <string>
#include <string_view>

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
    //
    // The lines are read as they come rather than parsed into objects
    // of their own.  A screen is a few hundred of them and a screen is
    // drawn many times a second, and building an object for each field
    // of each of them costs as much as the drawing does.  Nothing here
    // is kept once it has been read, so nothing is allocated for it.
    class DrawReader
    {
    public:
        // Take one line, which is one thing to draw.  Returns the
        // screen when the line ends one, and nothing while one is
        // still being read.
        std::optional<core::window::DrawFrame> Take(std::string_view line);

        // Why the last line was not understood, or nothing.
        std::wstring const& Why() const { return m_why; }

    private:
        core::window::DrawFrame m_frame;
        bool m_begun{ false };
        std::wstring m_why;
    };
}
