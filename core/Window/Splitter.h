#pragma once

#include "Input/Pointer.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>

namespace urusi::core::window
{
    // A splitter Lisp put between two parts of a layout. It is named
    // urusi-splitter:DIRECTION:BEFORE:AFTER, DIRECTION being h for one
    // between parts side by side and v for one between parts one above
    // the other; BEFORE and AFTER are the ids of the parts.
    struct SplitterName
    {
        bool horizontal{ false };
        std::wstring before;
        std::wstring after;
    };

    // NAME read as a splitter's, or nothing if it is not one.
    inline std::optional<SplitterName> ParseSplitter(std::wstring const& name)
    {
        constexpr std::wstring_view kPrefix = L"urusi-splitter:";

        if (name.rfind(kPrefix, 0) != 0 || name.size() < kPrefix.size() + 1)
        {
            return std::nullopt;
        }

        wchar_t direction = name[kPrefix.size()];
        if (direction != L'h' && direction != L'v')
        {
            return std::nullopt;
        }

        SplitterName out;
        out.horizontal = direction == L'h';

        auto rest = name.substr(kPrefix.size() + 1);
        if (!rest.empty() && rest[0] == L':')
        {
            rest.erase(0, 1);
            auto colon = rest.find(L':');
            out.before = rest.substr(0, colon);
            if (colon != std::wstring::npos)
            {
                out.after = rest.substr(colon + 1);
            }
        }
        return out;
    }

    // The parts either side of a splitter, as the grid they are in has
    // them: the column or the row each is in.
    struct ISplitterTracks
    {
        virtual ~ISplitterTracks() = default;

        // How long the part before the splitter, or after it, is now.
        virtual double Length(bool before) const = 0;

        // Whether it shares what the grid has left, rather than having a
        // size of its own.
        virtual bool Shares(bool before) const = 0;

        // Give it a size of its own.
        virtual void SetLength(bool before, double length) = 0;
    };

    // A splitter being dragged: the parts either side made bigger and
    // smaller as it goes, and told where it ended when it is let go.
    //
    // The part with a size of its own is the one resized, and the one
    // that shares what is left takes the rest; if neither has one, the
    // one before is given one. Neither is made shorter than kLeast.
    class Splitter
    {
    public:
        static constexpr double kLeast = 40;

        struct Events
        {
            // A drag began: the frames around are not to be resized
            // while it goes on, which would have Emacs lay out and draw
            // again with each step.
            std::function<void()> started;

            // It ended, the parts BEFORE and AFTER long.
            std::function<void(double before, double after)> finished;
        };

        Splitter(SplitterName name, input::IPointerInputDevice& pointer, ISplitterTracks& tracks,
                 Events events)
            : m_name(std::move(name)), m_pointer(pointer), m_tracks(tracks),
              m_events(std::move(events))
        {
        }

        SplitterName const& Name() const noexcept { return m_name; }
        bool Dragging() const noexcept { return m_dragging; }

        // Take what the pointer did over the splitter, where it is
        // counted from the corner of the grid the parts are in. Return
        // whether it was the splitter's to take.
        bool Handle(input::PointerEvent const& event)
        {
            switch (event.kind)
            {
            case input::PointerKind::Pressed:
                if (event.button != input::PointerButton::Left)
                {
                    return false;
                }
                m_dragging = true;
                m_start = Along(event);
                m_before = m_tracks.Length(true);
                m_after = m_tracks.Length(false);
                m_pointer.Capture();
                if (m_events.started)
                {
                    m_events.started();
                }
                return true;

            case input::PointerKind::Moved:
                if (!m_dragging)
                {
                    return false;
                }
                Resize(Along(event) - m_start);
                return true;

            case input::PointerKind::Released:
            case input::PointerKind::CaptureLost:
                if (!m_dragging)
                {
                    return false;
                }
                m_dragging = false;
                if (event.kind == input::PointerKind::Released)
                {
                    m_pointer.Release();
                }
                if (m_events.finished)
                {
                    m_events.finished(m_tracks.Length(true), m_tracks.Length(false));
                }
                return true;

            case input::PointerKind::Wheel:
                return false;
            }
            return false;
        }

    private:
        double Along(input::PointerEvent const& event) const noexcept
        {
            return m_name.horizontal ? event.x : event.y;
        }

        void Resize(double moved)
        {
            // Parts already shorter than that between them do not move
            // at all: there is no room to give either.
            double least = kLeast - m_before;
            double most = m_after - kLeast;
            moved = least > most ? 0 : std::clamp(moved, least, most);

            if (m_tracks.Shares(true) && !m_tracks.Shares(false))
            {
                m_tracks.SetLength(false, m_after - moved);
            }
            else
            {
                m_tracks.SetLength(true, m_before + moved);
            }
        }

        SplitterName m_name;
        input::IPointerInputDevice& m_pointer;
        ISplitterTracks& m_tracks;
        Events m_events;

        bool m_dragging{ false };
        double m_start{ 0 };
        double m_before{ 0 };
        double m_after{ 0 };
    };
}
