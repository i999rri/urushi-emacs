#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace urusi
{
    // What the input method is turning over, and where it is, as the
    // input method counts.
    //
    // The context the input method talks to never forgets how much has
    // gone through it: each composition begins where the last ended,
    // and it asks for text and says what changed by those positions.
    // Emptying the text and telling it so leaves it unable to begin
    // another. So nothing is emptied: what has been handed on is
    // counted, the text of it answered with spaces, and only what is
    // being composed is kept.
    //
    // Pure, so that the counting can be tested without an input method.
    class ImeBuffer
    {
    public:
        // Replace the text from START to END, positions as the input
        // method counts, with TEXT.
        void Update(int32_t start, int32_t end, std::wstring const& text)
        {
            // With nothing being composed, where the input method puts
            // the text is where what was handed on ends, as it counts.
            // Taken from it, not counted here: it does not always
            // start again from nothing when the focus comes back, and a
            // count of our own that differs by one turns a replacement
            // into an addition, and the text arrives twice.
            if (m_text.empty())
            {
                m_handedOn = (std::max)(start, 0);
            }

            int32_t size = static_cast<int32_t>(m_text.size());
            int32_t from = std::clamp(start - m_handedOn, 0, size);
            int32_t to = std::clamp(end - m_handedOn, from, size);

            m_text.replace(static_cast<size_t>(from), static_cast<size_t>(to - from), text);
        }

        void Started()
        {
            m_composing = true;
            m_text.clear();
        }

        // What was settled on, which is counted as handed on and
        // forgotten. It is what the composition came to, or the text
        // that came without one, as a key typed with the input method
        // open and idle does.
        std::wstring Settle()
        {
            std::wstring settled = std::move(m_text);

            m_composing = false;
            m_handedOn += static_cast<int32_t>(settled.size());
            m_text.clear();
            return settled;
        }

        // Start again from nothing, as the input method does when the
        // focus comes back or is taken away.
        void Reset()
        {
            m_composing = false;
            m_text.clear();
            m_handedOn = 0;
        }

        // The text from START to END, positions as the input method
        // counts: spaces for what has been handed on.
        std::wstring Text(int32_t start, int32_t end) const
        {
            std::wstring all(static_cast<size_t>(m_handedOn), L' ');
            all += m_text;

            int32_t size = static_cast<int32_t>(all.size());
            int32_t from = std::clamp(start, 0, size);
            int32_t to = std::clamp(end, from, size);
            return all.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
        }

        // Where the caret is, as the input method counts: after what is
        // being composed.
        int32_t Caret() const noexcept
        {
            return m_handedOn + static_cast<int32_t>(m_text.size());
        }

        std::wstring const& Composed() const noexcept { return m_text; }
        bool Composing() const noexcept { return m_composing; }

    private:
        std::wstring m_text;
        int32_t m_handedOn{ 0 };
        bool m_composing{ false };
    };
}
