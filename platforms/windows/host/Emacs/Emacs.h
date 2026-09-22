#pragma once

#include <winrt/Windows.Data.Json.h>

#include <chrono>
#include <functional>
#include <set>
#include <string>

namespace urusi::windows::emacs
{
    // Emacs, as the window talks to it: starting it, the messages that
    // go to it and come from it, and whether it can answer yet.
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

        // Start Emacs in this process, the user's Emacs with the user's
        // init file. OUTPUT is what it writes before it can send
        // messages, and is called on a thread of Emacs's.
        void Start(std::function<void(std::string)> output);

        // The message LINE is, or null if it is not one, which Emacs is
        // told.
        winrt::Windows::Data::Json::JsonObject Receive(std::string const& line);

        // Answer Emacs's hello, after which it can be asked things.
        // DEBUG says whether the keyboard is being written down, SCALE
        // how many pixels of the screen go to one of XAML's.
        void Hello(bool debug, double scale);
        bool Ready() const noexcept { return m_ready; }

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
        bool m_ready{ false };
        std::set<std::wstring> m_seen;
        std::chrono::steady_clock::time_point m_lastHeard{};
        std::chrono::steady_clock::time_point m_closeAsked{};
    };
}
