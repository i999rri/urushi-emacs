#pragma once

#include "Emacs/EmacsConnection.h"

namespace urushi::windows::emacs
{
    // Emacs loaded into this process from libemacs.dll, talking through
    // the table of host.h: EmacsHost starts it and HostApi carries the
    // messages. Which window system it draws with is the DLL's to say,
    // and it says so in its hello.
    class InProcessEmacs : public EmacsConnection
    {
    public:
        void OnMessage(MessageFn fn) override;
        void Start(OutputFn output, ExitFn exited) override;
        void Send(std::string const& message) override;
    };
}
