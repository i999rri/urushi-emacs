#include "pch.h"
#include "Emacs/HostApi.h"

namespace
{
    void HostPost(char const* message)
    {
        urusi::windows::emacs::HostApi::Instance().Deliver(message);
    }

    void HostOnEvent(host_event_fn fn, void* data)
    {
        urusi::windows::emacs::HostApi::Instance().SetSink(fn, data);
    }

    constexpr host_api kApi{ HOST_API_VERSION, HostPost, HostOnEvent };
}

// What Emacs looks for in this executable, and the only thing it does.
extern "C" __declspec(dllexport) host_api const* host_get_api(unsigned version)
{
    return version == HOST_API_VERSION ? &kApi : nullptr;
}

namespace urusi::windows::emacs
{
    HostApi& HostApi::Instance()
    {
        // Emacs keeps the address of the table past the life of any
        // window, so this one outlives everything on purpose.
        static HostApi* instance = new HostApi{};
        return *instance;
    }

    void HostApi::OnMessage(MessageFn fn)
    {
        std::lock_guard<std::mutex> held{ m_lock };
        m_onMessage = std::move(fn);
    }

    void HostApi::Deliver(char const* message)
    {
        MessageFn handler;
        {
            std::lock_guard<std::mutex> held{ m_lock };
            handler = m_onMessage;
        }

        // Emacs can post before the window is listening, and does: it
        // says hello as it loads urusi.el.
        if (handler && message)
        {
            handler(message);
        }
    }

    void HostApi::SetSink(host_event_fn fn, void* data)
    {
        std::lock_guard<std::mutex> held{ m_lock };
        m_sink = fn;
        m_sinkData = data;
    }

    void HostApi::Send(std::string const& message)
    {
        host_event_fn sink = nullptr;
        void* data = nullptr;
        {
            std::lock_guard<std::mutex> held{ m_lock };
            sink = m_sink;
            data = m_sinkData;
        }

        if (sink)
        {
            sink(data, message.c_str());
        }
    }
}
