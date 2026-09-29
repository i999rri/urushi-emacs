#pragma once

#include <functional>
#include <string>

namespace urushi::windows::emacs
{
    // Whether a dialog is up, and what to do once one is not.
    //
    // Only one can be up: a second laid under the first is one nobody
    // can answer.  That is a piece of state, and state that decides
    // whether something is shown is kept in something that owns it
    // rather than beside the code that reads it: one left set by a
    // dialog that went away without saying so is a window that never
    // asks anything again, and nothing would say why.
    //
    // A window that draws its own title bar has also told Windows which
    // parts of it can be clicked, and a dialog puts that out; so
    // whoever owns this is told when one ends and can say it again.
    //
    // UI thread only.
    class Asking
    {
    public:
        // Whether a dialog is up now.
        bool Busy() const { return m_up; }

        // Take the one place there is for a dialog, or say it is taken.
        bool Begin()
        {
            if (m_up)
            {
                return false;
            }
            m_up = true;
            return true;
        }

        // Give it back, and say so.
        void End()
        {
            if (!m_up)
            {
                return;
            }
            m_up = false;
            if (m_ended)
            {
                m_ended();
            }
        }

        // Called each time a dialog ends, for whatever it put out.
        void OnEnded(std::function<void()> ended) { m_ended = std::move(ended); }

    private:
        bool m_up{ false };
        std::function<void()> m_ended;
    };
}
