#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Data.Json.h>

#include <functional>
#include <string>

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

        // Do METHOD with ARGS on WINDOW, and REPLY when it is done.
        static void Call(winrt::Microsoft::UI::Xaml::Window const& window,
                         std::wstring const& method,
                         winrt::Windows::Data::Json::JsonObject const& args,
                         Reply reply);
    };
}
