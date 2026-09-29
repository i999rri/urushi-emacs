#include "pch.h"
#include "Emacs/Emacs.h"

#include "Emacs/EmacsProcess.h"
#include "Emacs/InProcessEmacs.h"

using namespace winrt;
using namespace Windows::Data::Json;

namespace
{
    JsonValue String(hstring const& text)
    {
        return JsonValue::CreateStringValue(text);
    }
}

namespace urushi::windows::emacs
{
    Emacs::Emacs(Events events)
        : m_events(std::move(events)), m_command(EmacsProcess::ConfiguredCommand())
    {
        if (m_command.empty())
        {
            m_connection = std::make_shared<InProcessEmacs>();
        }
        else
        {
            m_connection = std::make_shared<EmacsProcess>(std::wstring{ to_hstring(m_command) });
        }
    }

    void Emacs::OnMessage(EmacsConnection::MessageFn fn)
    {
        m_connection->OnMessage(std::move(fn));
    }

    void Emacs::Start(EmacsConnection::OutputFn output, EmacsConnection::ExitFn exited)
    {
        Log("host", m_command.empty() ? std::string{ "Emacs in this process\n" }
                                      : "Emacs as a process of its own: " + m_command + "\n");
        m_connection->Start(std::move(output), std::move(exited));
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

    void Emacs::Hello(JsonObject const& said, bool debug, double scale)
    {
        m_ready = true;
        m_windowSystem = said.GetNamedString(L"window-system", L"");
        Log("host", "Emacs draws with " + to_string(m_windowSystem) + "\n");

        JsonObject reply;
        reply.SetNamedValue(L"type", String(L"hello"));
        reply.SetNamedValue(L"host", String(L"urushi-emacs"));
        reply.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
        reply.SetNamedValue(L"debug", JsonValue::CreateBooleanValue(debug));
        // Emacs measures in the pixels of the screen and XAML in 96ths
        // of an inch, and this is what lies between them.
        reply.SetNamedValue(L"scale", JsonValue::CreateNumberValue(scale));
        // This host draws what Emacs says to draw, so Emacs need not
        // draw it and hand over the pixels.  Emacs asks because a host
        // that cannot would show nothing at all.
        reply.SetNamedValue(L"draws", JsonValue::CreateBooleanValue(true));
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
        m_connection->Send(to_string(message.Stringify()));
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
