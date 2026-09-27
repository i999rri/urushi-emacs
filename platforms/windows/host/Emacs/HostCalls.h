#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Data.Json.h>

#include <functional>
#include <string>

#include "Emacs/Asking.h"

namespace urusi::windows::emacs
{
    // What Lisp can ask the host to do, besides draw.
    //
    // Lisp sends {"type":"call","id":N,"method":M,"args":{...}}, and the
    // host answers {"type":"reply","id":N,"value":V}, or "error" in
    // place of "value". A method that waits on the person using the
    // application, such as one that asks them for a file, answers when
    // they have, and Emacs goes on in the meantime.
    //
    // UI thread only.
    class HostCalls
    {
    public:
        // Called once with the answer, or with an error and a null value.
        using Reply = std::function<void(winrt::Windows::Data::Json::IJsonValue const& value,
                                         std::wstring const& error)>;

        // Where to write what happened, for the log the window keeps.
        // Nothing is written until this is given one.
        static void OnLog(std::function<void(std::string const&)> log);
        static void Log(std::string const& what);

        // What a method has to work with: the window it is about, and
        // what is already up on it.  Held by whoever owns the window
        // rather than here.
        struct Where
        {
            winrt::Microsoft::UI::Xaml::Window window{ nullptr };
            Asking& asking;
        };

        // Do METHOD with ARGS where WHERE says, and REPLY when it is
        // done.
        static void Call(Where const& where,
                         std::wstring const& method,
                         winrt::Windows::Data::Json::JsonObject const& args,
                         Reply reply);
    };
}
