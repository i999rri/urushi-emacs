#pragma once

#include "Emacs/EmacsConnection.h"

#include <winrt/Windows.Data.Json.h>

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace urusi::windows::emacs
{
    // Emacs, as the window talks to it: starting it, the messages that
    // go to it and come from it, and whether it can answer yet.
    //
    // Which Emacs it is is chosen as this is made: the one loaded into
    // this process, unless %USERPROFILE%\.urusi-emacs-remote names a
    // command that starts one as a process of its own
    // (docs/remote.md).
    //
    // What a message asks for is the window's to do; this only reads
    // it, and says what it has to say back. Send, SendEvent,
    // SendHostEvent and Reply are safe from any thread, as HostApi is;
    // the rest is for the UI thread.
    class Emacs
    {
    public:
        struct Events
        {
            // A line for the log, and which side it is from: "host" or
            // "emacs".
            std::function<void(char const* source, std::string const& text)> log;
            // Something went wrong that the person at the window is to
            // see, as well as Emacs.
            std::function<void(winrt::hstring const& text)> error;
        };

        explicit Emacs(Events events);

        // Where the messages from Emacs go, a line of JSON each. Called
        // on a thread of Emacs's; set before Start.
        void OnMessage(EmacsConnection::MessageFn fn);

        // Start Emacs, the user's Emacs with the user's init file.
        // OUTPUT is given what it writes before it can send messages,
        // and what goes wrong in starting it, and EXITED is called if it
        // goes; both on a thread that is not the window's.
        void Start(EmacsConnection::OutputFn output, EmacsConnection::ExitFn exited);

        // Whether the keys, the pointer and the focus go to Emacs as
        // messages rather than to the windows of its frames. Only the
        // w32 window system makes frames that are windows of this
        // process, which is the one case where there is anything to
        // post to; Emacs says which it draws with in its hello, and
        // until then there is no frame to tell either way.
        bool InputAsMessages() const noexcept { return m_windowSystem != L"w32"; }

        // The message LINE is, or null if it is not one, which Emacs is
        // told.
        winrt::Windows::Data::Json::JsonObject Receive(std::string const& line);

        // Answer Emacs's hello, SAID, after which it can be asked
        // things. DEBUG says whether the keyboard is being written
        // down, SCALE how many pixels of the screen go to one of
        // XAML's.
        void Hello(winrt::Windows::Data::Json::JsonObject const& said,
                   bool debug, double scale);
        bool Ready() const noexcept { return m_ready; }

        // Emacs has gone, and cannot be asked anything again.
        void Exited() noexcept { m_ready = false; }

        // Whether closing the window is to wait for Emacs, which is then
        // asked to close it: it asks about the buffers that are not
        // saved first, and may be told not to. Not until Emacs can
        // answer, nor once it has gone quiet since it was asked, since a
        // window that cannot be closed is worse.
        bool AskToClose();

        void Send(winrt::Windows::Data::Json::JsonObject const& message) const;
        // What happened to an element Lisp built, to the handler of ID.
        void SendEvent(winrt::hstring const& id,
                       winrt::Windows::Data::Json::JsonObject const& args) const;
        // What happened to the window that Lisp did not ask for.
        void SendHostEvent(winrt::hstring const& name,
                           winrt::Windows::Data::Json::JsonObject const& details) const;
        void SendError(winrt::hstring const& text) const;
        // The answer to call ID: VALUE, or ERROR if it is not empty.
        void Reply(double id, winrt::Windows::Data::Json::IJsonValue const& value,
                   std::wstring const& error) const;

    private:
        void Log(char const* source, std::string const& text) const;

        Events m_events;
        std::shared_ptr<EmacsConnection> m_connection;
        // The command the Emacs of its own process is started with, or
        // empty for the one in this process.
        std::string m_command;
        // What Emacs draws its frames with, from its hello.
        winrt::hstring m_windowSystem;
        bool m_ready{ false };
        std::set<std::wstring> m_seen;
        std::chrono::steady_clock::time_point m_lastHeard{};
        std::chrono::steady_clock::time_point m_closeAsked{};
    };
}
