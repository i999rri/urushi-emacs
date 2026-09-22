#include "pch.h"
#include "Emacs/Emacs.h"

#include "Emacs/EmacsHost.h"
#include "Emacs/HostApi.h"

using namespace winrt;
using namespace Windows::Data::Json;

namespace
{
    JsonValue String(hstring const& text)
    {
        return JsonValue::CreateStringValue(text);
    }

    // What the standard handle WHICH of this process is: none, or what
    // kind of thing, and whether a child is handed it.
    std::string DescribeHandle(DWORD which)
    {
        HANDLE handle = GetStdHandle(which);
        if (!handle || handle == INVALID_HANDLE_VALUE)
        {
            return "none";
        }

        DWORD flags = 0;
        GetHandleInformation(handle, &flags);
        char const* kinds[] = { "unknown", "disk", "char", "pipe" };
        DWORD kind = GetFileType(handle) & ~FILE_TYPE_REMOTE;
        return std::string{ kind < 4 ? kinds[kind] : "other" }
            + ((flags & HANDLE_FLAG_INHERIT) ? ", inheritable" : "");
    }
}

namespace urusi::windows::emacs
{
    Emacs::Emacs(Events events) : m_events(std::move(events))
    {
    }

    void Emacs::Start(std::function<void(std::string)> output)
    {
        // No -Q: this is the user's Emacs, and it reads the user's init
        // file like any other. Nothing is said here about the screen:
        // site-start.el brings urusi up before the init file, so that
        // the init file can say what the screen should look like, and
        // shows it once the init file has.
        std::vector<std::string> args{ "emacs" };

        // What this process was started with, before any of it is
        // changed. It depends on what started the application, and
        // Emacs hands it on to every program it runs.
        Log("host", "started with stdin " + DescribeHandle(STD_INPUT_HANDLE)
            + ", stdout " + DescribeHandle(STD_OUTPUT_HANDLE)
            + ", stderr " + DescribeHandle(STD_ERROR_HANDLE) + "\n");

        // And whether whatever started it handed over a C runtime's
        // table of open files, which the runtime Emacs uses reads as
        // its own descriptors as it starts.
        STARTUPINFOW startup{ sizeof(startup) };
        GetStartupInfoW(&startup);
        Log("host", "startup info flags 0x" + std::to_string(startup.dwFlags)
            + ", runtime table " + std::to_string(startup.cbReserved2) + " bytes\n");

        std::string error;
        if (!EmacsHost::Instance().Start(EmacsHost::DefaultDll(), args, std::move(output), error))
        {
            Log("host", error + "\n");
        }
    }

    JsonObject Emacs::Receive(std::string const& line)
    {
        JsonObject message{ nullptr };
        if (!JsonObject::TryParse(to_hstring(line), message))
        {
            SendError(L"invalid JSON");
            return nullptr;
        }

        auto type = message.GetNamedString(L"type", L"");
        m_lastHeard = std::chrono::steady_clock::now();

        // The first of each kind, so that a message that never comes is
        // as plain to see as one that fails.
        if (m_seen.insert(std::wstring{ type }).second)
        {
            Log("host", "first " + to_string(type) + ", " + std::to_string(line.size())
                + " bytes\n");
        }
        return message;
    }

    void Emacs::Hello(bool debug, double scale)
    {
        m_ready = true;

        JsonObject reply;
        reply.SetNamedValue(L"type", String(L"hello"));
        reply.SetNamedValue(L"host", String(L"urusi-emacs"));
        reply.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
        reply.SetNamedValue(L"debug", JsonValue::CreateBooleanValue(debug));
        // Emacs measures in the pixels of the screen and XAML in 96ths
        // of an inch, and this is what lies between them.
        reply.SetNamedValue(L"scale", JsonValue::CreateNumberValue(scale));
        Send(reply);
    }

    bool Emacs::AskToClose()
    {
        constexpr std::chrono::seconds kPatience{ 5 };

        if (!m_ready)
        {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        bool asked = m_closeAsked != std::chrono::steady_clock::time_point{};
        bool silent = asked && m_lastHeard < m_closeAsked && now - m_closeAsked > kPatience;
        if (silent)
        {
            return false;
        }

        m_closeAsked = now;
        SendHostEvent(L"close", JsonObject{});
        return true;
    }

    void Emacs::Send(JsonObject const& message) const
    {
        HostApi::Instance().Send(to_string(message.Stringify()));
    }

    void Emacs::SendEvent(hstring const& id, JsonObject const& args) const
    {
        JsonObject message;
        message.SetNamedValue(L"type", String(L"event"));
        message.SetNamedValue(L"id", String(id));
        message.SetNamedValue(L"args", args);
        Send(message);
    }

    void Emacs::SendHostEvent(hstring const& name, JsonObject const& details) const
    {
        details.SetNamedValue(L"type", String(L"host-event"));
        details.SetNamedValue(L"event", String(name));
        Send(details);
    }

    void Emacs::SendError(hstring const& text) const
    {
        if (m_events.error)
        {
            m_events.error(text);
        }
        Log("host", "error: " + to_string(text) + "\n");

        JsonObject message;
        message.SetNamedValue(L"type", String(L"error"));
        message.SetNamedValue(L"message", String(text));
        Send(message);
    }

    void Emacs::Reply(double id, IJsonValue const& value, std::wstring const& error) const
    {
        JsonObject reply;
        reply.SetNamedValue(L"type", String(L"reply"));
        reply.SetNamedValue(L"id", JsonValue::CreateNumberValue(id));
        if (error.empty())
        {
            reply.SetNamedValue(L"value", value);
        }
        else
        {
            reply.SetNamedValue(L"error", String(hstring{ error }));
        }
        Send(reply);
    }

    void Emacs::Log(char const* source, std::string const& text) const
    {
        if (m_events.log)
        {
            m_events.log(source, text);
        }
    }
}
