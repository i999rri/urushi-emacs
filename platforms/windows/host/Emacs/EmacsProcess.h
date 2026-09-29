#pragma once

#include "Emacs/EmacsConnection.h"

#include <windows.h>

#include <memory>
#include <mutex>
#include <string>

namespace urushi::windows::emacs
{
    // Emacs as a process of its own, talking the protocol a line to a
    // message over its standard input and output: an Emacs on the host
    // window system, which has none of Windows's windows, as the Emacs
    // for Linux run in WSL is.
    //
    // What it writes to its standard error goes to the log. A thread
    // reads each of its outputs, and holds this until the process has
    // gone; the process goes with the application, whose job it is in.
    class EmacsProcess : public EmacsConnection, public std::enable_shared_from_this<EmacsProcess>
    {
    public:
        // COMMAND is the command line to start, as CreateProcess takes it.
        explicit EmacsProcess(std::wstring command);
        ~EmacsProcess() override;

        EmacsProcess(EmacsProcess const&) = delete;
        EmacsProcess& operator=(EmacsProcess const&) = delete;

        // The command line %USERPROFILE%\.urushi-emacs-remote says to
        // start, or empty if there is no such file or no command in it.
        // A file, because a packaged application is given none of the
        // environment it is started from.
        static std::string ConfiguredCommand();

        void OnMessage(MessageFn fn) override;
        void Start(OutputFn output, ExitFn exited) override;
        void Send(std::string const& message) override;

    private:
        bool Launch(std::string& error);
        void ReadMessages();
        void ReadErrors();
        void Deliver(std::string line);

        std::wstring m_command;
        OutputFn m_output;
        ExitFn m_exited;

        std::mutex m_lock;
        MessageFn m_onMessage;

        // Messages are written whole, one at a time, from any thread.
        std::mutex m_writing;
        HANDLE m_input{ nullptr };
        HANDLE m_messages{ nullptr };
        HANDLE m_errors{ nullptr };
        HANDLE m_process{ nullptr };
        HANDLE m_job{ nullptr };
    };
}
