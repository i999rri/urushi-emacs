#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace urusi::core::text
{
    // Chunks as they arrive, given back a whole line at a time.
    //
    // Emacs on the end of a pipe sends a message to a line, and a line
    // arrives in as many pieces as the pipe cares to give it.  What
    // holds the pieces has to remember how far along it has looked for
    // the end of a line: looking from the beginning of what it holds
    // each time reads the same bytes again and again, which for a line
    // that takes many reads is the line over and over.
    //
    // The file of a font is what makes that matter.  It comes to a
    // hundred megabytes on one line, in two thousand reads of sixty-four
    // kilobytes, and looking again each time comes to a hundred
    // gigabytes of looking -- tens of seconds in which nothing else is
    // read from Emacs, so the window stands still and the font never
    // arrives.  Here every byte is looked at once.
    class Lines
    {
    public:
        // Take CHUNK, and call SAY with each line it completes. The
        // line has no newline on it, nor the carriage return before
        // one; it belongs to SAY, which may take it.
        template <class Say>
        void Take(std::string_view chunk, Say say)
        {
            m_pending.append(chunk);

            size_t start = 0;
            for (auto end = m_pending.find('\n', m_searched); end != std::string::npos;
                 end = m_pending.find('\n', start))
            {
                std::string line = m_pending.substr(start, end - start);

                start = end + 1;
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                say(std::move(line));
            }
            // Only where a line ended.  An erase of nothing is not
            // nothing: it moves what follows what it did not erase,
            // which is the whole of what is held -- four milliseconds
            // on a hundred megabytes, on every read, and that is the
            // greater half of what used to be spent here.
            if (start > 0)
            {
                m_pending.erase(0, start);
            }
            // What is left has no newline in it, so the next chunk is
            // where the next one can first be.
            m_searched = m_pending.size();
        }

        // What has come in that no newline has ended yet.
        size_t Held() const noexcept { return m_pending.size(); }

    private:
        std::string m_pending;
        size_t m_searched{ 0 };
    };
}
