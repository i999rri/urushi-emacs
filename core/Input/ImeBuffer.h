#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace urusi::core::input
{
    // What the input method is turning over.
    //
    // Only that: what it settles on is typed into Emacs, where the
    // input method cannot follow, and what it is composing now begins
    // from nothing each time.
    //
    // Pure, so that it can be tested without an input method.
    class ImeBuffer
    {
    public:
        // Replace the text from START to END with TEXT, as the input
        // method counts the positions: from the start of what is being
        // composed.
        void Update(int32_t start, int32_t end, std::wstring const& text)
        {
            int32_t size = static_cast<int32_t>(m_text.size());
            int32_t from = std::clamp(start, 0, size);
            int32_t to = std::clamp(end, from, size);

            m_text.replace(static_cast<size_t>(from), static_cast<size_t>(to - from), text);
        }

        void Started()
        {
            m_composing = true;
            m_text.clear();
        }

        // What was settled on, which is forgotten here: Emacs has it
        // now.
        std::wstring Settle()
        {
            std::wstring settled = std::move(m_text);

            m_composing = false;
            m_text.clear();
            return settled;
        }

        // Start again from nothing, as the input method does when the
        // focus comes back or is taken away.
        void Reset()
        {
            m_composing = false;
            m_text.clear();
        }

        std::wstring const& Composed() const noexcept { return m_text; }
        bool Composing() const noexcept { return m_composing; }

    private:
        std::wstring m_text;
        bool m_composing{ false };
    };
}
