#pragma once

#include <functional>
#include <string>

namespace urusi::windows::emacs
{
    // How the application reaches Emacs: the messages that go each way,
    // and starting it.
    //
    // Emacs is either loaded into this process, where its frames are
    // windows of its own that the keys and the pointer are posted to,
    // or a process of its own on the host window system, which has no
    // windows and takes them as messages. Everything else is messages
    // either way.
    class EmacsConnection
    {
    public:
        // One message from Emacs, a line of JSON. Called on a thread
        // that is not the window's.
        using MessageFn = std::function<void(std::string)>;
        // A line for the log, and which side it is from: "host" or
        // "emacs". Called on any thread.
        using OutputFn = std::function<void(char const* source, std::string text)>;
        // Emacs has gone. Called on a thread that is not the window's.
        using ExitFn = std::function<void()>;

        virtual ~EmacsConnection() = default;

        // Where the messages from Emacs go, set before it starts.
        virtual void OnMessage(MessageFn fn) = 0;

        // Start Emacs, saying what it writes, and what goes wrong, to
        // OUTPUT.
        virtual void Start(OutputFn output, ExitFn exited) = 0;

        // Send one message to Emacs. Safe from any thread.
        virtual void Send(std::string const& message) = 0;

        // Whether the keys, the pointer and the focus go to Emacs as
        // messages, rather than as the Windows messages of its frames'
        // windows.
        virtual bool InputAsMessages() const noexcept = 0;
    };
}
