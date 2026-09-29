#pragma once

#include <string>
#include <string_view>

namespace urushi::windows::emacs
{
    // The command line in TEXT, the contents of the file that says to
    // start Emacs as a process of its own: its first line that is not
    // empty and not a comment, which starts with #. Empty if there is
    // none, which is the same as no file.
    inline std::string RemoteCommand(std::string_view text)
    {
        constexpr std::string_view kSpace = " \t\r\n";
        constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

        // A file saved from Notepad may start with a byte order mark.
        if (text.substr(0, kByteOrderMark.size()) == kByteOrderMark)
        {
            text.remove_prefix(kByteOrderMark.size());
        }

        while (!text.empty())
        {
            auto end = text.find('\n');
            auto line = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);

            auto first = line.find_first_not_of(kSpace);
            if (first == std::string_view::npos || line[first] == '#')
            {
                continue;
            }
            auto last = line.find_last_not_of(kSpace);
            return std::string{ line.substr(first, last - first + 1) };
        }
        return {};
    }
}
