#pragma once

#include <cctype>
#include <map>
#include <string>
#include <variant>
#include <vector>

// Enough JSON to read a trace: one object to a line, whose values are
// strings, numbers, booleans, or arrays of strings. Anything else is a
// line that is not understood, and says so.
namespace trace
{
    using Value = std::variant<std::string, double, bool, std::vector<std::string>>;
    using Line = std::map<std::string, Value>;

    class Reader
    {
    public:
        explicit Reader(std::string const& text) : m_text(text) {}

        bool Object(Line& line)
        {
            Space();
            if (!Take('{'))
            {
                return false;
            }
            Space();
            if (Take('}'))
            {
                return true;
            }
            for (;;)
            {
                std::string key;
                Value value;
                Space();
                if (!String(key))
                {
                    return false;
                }
                Space();
                if (!Take(':') || !ReadValue(value))
                {
                    return false;
                }
                line[key] = std::move(value);
                Space();
                if (Take('}'))
                {
                    return true;
                }
                if (!Take(','))
                {
                    return false;
                }
            }
        }

    private:
        bool ReadValue(Value& value)
        {
            Space();
            if (Peek() == '"')
            {
                std::string text;
                if (!String(text))
                {
                    return false;
                }
                value = text;
                return true;
            }
            if (Take('['))
            {
                std::vector<std::string> items;
                Space();
                if (Take(']'))
                {
                    value = items;
                    return true;
                }
                for (;;)
                {
                    std::string item;
                    Space();
                    if (!String(item))
                    {
                        return false;
                    }
                    items.push_back(item);
                    Space();
                    if (Take(']'))
                    {
                        value = items;
                        return true;
                    }
                    if (!Take(','))
                    {
                        return false;
                    }
                }
            }
            if (m_text.compare(m_at, 4, "true") == 0)
            {
                m_at += 4;
                value = true;
                return true;
            }
            if (m_text.compare(m_at, 5, "false") == 0)
            {
                m_at += 5;
                value = false;
                return true;
            }

            size_t start = m_at;
            while (m_at < m_text.size()
                   && (std::isdigit(static_cast<unsigned char>(m_text[m_at]))
                       || m_text[m_at] == '-' || m_text[m_at] == '.'))
            {
                ++m_at;
            }
            if (start == m_at)
            {
                return false;
            }
            value = std::stod(m_text.substr(start, m_at - start));
            return true;
        }

        bool String(std::string& out)
        {
            if (!Take('"'))
            {
                return false;
            }
            while (m_at < m_text.size())
            {
                char c = m_text[m_at++];
                if (c == '"')
                {
                    return true;
                }
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (m_at >= m_text.size())
                {
                    return false;
                }
                char escaped = m_text[m_at++];
                switch (escaped)
                {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    if (m_at + 4 > m_text.size())
                    {
                        return false;
                    }
                    unsigned code = std::stoul(m_text.substr(m_at, 4), nullptr, 16);
                    m_at += 4;
                    // Control characters are all a trace escapes this way.
                    out += static_cast<char>(code);
                    break;
                }
                default: out += escaped;
                }
            }
            return false;
        }

        void Space()
        {
            while (m_at < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_at])))
            {
                ++m_at;
            }
        }

        char Peek() const { return m_at < m_text.size() ? m_text[m_at] : '\0'; }

        bool Take(char c)
        {
            if (Peek() != c)
            {
                return false;
            }
            ++m_at;
            return true;
        }

        std::string const& m_text;
        size_t m_at{ 0 };
    };
}
