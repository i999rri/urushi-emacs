#pragma once

// The interface itself, from the Emacs this application loads.
#include "../../../../external/emacs/libemacs/src/host.h"

#include <functional>
#include <mutex>
#include <string>

namespace urusi::windows::emacs
{
    // This application's side of host.h.
    //
    // Emacs asks for the interface as it starts, which is before the
    // window has anything to show, so the two sides find each other
    // here rather than through each other. Emacs calls in on its own
    // thread and the window answers on the UI thread, so both ends are
    // behind a lock.
    class HostApi
    {
    public:
        using MessageFn = std::function<void(std::string)>;

        // One per process, as the exported entry point has nowhere to
        // take an instance from.
        static HostApi& Instance();

        // Where the messages from Emacs go. Called on Emacs's thread.
        void OnMessage(MessageFn fn);

        // Send one message to Emacs. Safe from any thread, and quietly
        // dropped until Emacs asks for its messages.
        void Send(std::string const& message);

        // For host_get_api, which is C and cannot reach the rest.
        void Deliver(char const* message);
        void SetSink(host_event_fn fn, void* data);

    private:
        HostApi() = default;

        std::mutex m_lock;
        MessageFn m_onMessage;
        host_event_fn m_sink{ nullptr };
        void* m_sinkData{ nullptr };
    };
}
