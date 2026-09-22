#pragma once

#include "Emacs/EmacsConnection.h"

namespace urusi::windows::emacs
{
    // Emacs loaded into this process from libemacs.dll, talking through
    // the table of host.h: EmacsHost starts it and HostApi carries the
    // messages. Its frames are windows of its own, which the keys and
    // the pointer are posted to.
    class InProcessEmacs : public EmacsConnection
    {
    public:
        void OnMessage(MessageFn fn) override;
        void Start(OutputFn output, ExitFn exited) override;
        void Send(std::string const& message) override;
        bool InputAsMessages() const noexcept override { return false; }
    };
}
