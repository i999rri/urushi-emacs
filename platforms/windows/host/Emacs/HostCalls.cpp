#include "pch.h"
#include "Emacs/HostCalls.h"

#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>

#include <map>

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Data::Json;

namespace
{
    using Reply = urusi::windows::emacs::HostCalls::Reply;
    using Method = void (*)(Window const&, JsonObject const&, Reply const&);

    void Done(Reply const& reply)
    {
        reply(JsonValue::CreateNullValue(), L"");
    }

    OverlappedPresenter Overlapped(AppWindow const& window)
    {
        // Out of full screen first: only the overlapped presenter can be
        // maximized, minimized or kept on top.
        if (window.Presenter().Kind() != AppWindowPresenterKind::Overlapped)
        {
            window.SetPresenter(AppWindowPresenterKind::Overlapped);
        }
        return window.Presenter().as<OverlappedPresenter>();
    }

    void Title(Window const& window, JsonObject const& args, Reply const& reply)
    {
        window.AppWindow().Title(args.GetNamedString(L"title", L""));
        Done(reply);
    }

    // How the window takes up the screen: "normal", "maximized",
    // "minimized" or "fullscreen", which are the ways Emacs's fullscreen
    // frame parameter and its iconified state can have a frame.
    void State(Window const& window, JsonObject const& args, Reply const& reply)
    {
        auto state = args.GetNamedString(L"state", L"normal");
        auto app = window.AppWindow();

        if (state == L"fullscreen")
        {
            app.SetPresenter(AppWindowPresenterKind::FullScreen);
        }
        else if (state == L"maximized")
        {
            Overlapped(app).Maximize();
        }
        else if (state == L"minimized")
        {
            Overlapped(app).Minimize();
        }
        else if (state == L"normal")
        {
            Overlapped(app).Restore();
        }
        else
        {
            reply(JsonValue::CreateNullValue(), L"no such window state: " + std::wstring{ state });
            return;
        }
        Done(reply);
    }

    void Topmost(Window const& window, JsonObject const& args, Reply const& reply)
    {
        Overlapped(window.AppWindow()).IsAlwaysOnTop(args.GetNamedBoolean(L"on", true));
        Done(reply);
    }

    // The size of the whole window, frame and all, in the pixels of
    // the screen: the size a person would drag it to.
    void Size(Window const& window, JsonObject const&, Reply const& reply)
    {
        auto size = window.AppWindow().Size();
        JsonObject value;
        value.SetNamedValue(L"width", JsonValue::CreateNumberValue(size.Width));
        value.SetNamedValue(L"height", JsonValue::CreateNumberValue(size.Height));
        reply(value, L"");
    }

    void Resize(Window const& window, JsonObject const& args, Reply const& reply)
    {
        window.AppWindow().Resize({ static_cast<int32_t>(args.GetNamedNumber(L"width", 800)),
                                    static_cast<int32_t>(args.GetNamedNumber(L"height", 600)) });
        Done(reply);
    }

    // Whether the window is being drawn dark, which is what the person
    // using it chose in Windows unless the application says otherwise.
    void Theme(Window const& window, JsonObject const&, Reply const& reply)
    {
        auto root = window.Content().try_as<FrameworkElement>();
        JsonObject value;
        value.SetNamedValue(L"dark", JsonValue::CreateBooleanValue(
            root && root.ActualTheme() == ElementTheme::Dark));
        reply(value, L"");
    }

    // Ask the person for a file, and answer with its name, or null if
    // they would rather not. This is the one that waits on them, so it
    // answers when they have and not before.
    fire_and_forget OpenFileAsync(Window window, JsonObject args, Reply reply)
    {
        try
        {
            Windows::Storage::Pickers::FileOpenPicker picker;
            HWND handle = nullptr;
            check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&handle));
            // A picker belongs to a window, and in a desktop application
            // it has to be told which.
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(handle));
            picker.FileTypeFilter().Append(L"*");

            auto file = co_await picker.PickSingleFileAsync();
            reply(file ? JsonValue::CreateStringValue(file.Path()) : JsonValue::CreateNullValue(), L"");
        }
        catch (hresult_error const& error)
        {
            reply(JsonValue::CreateNullValue(), std::wstring{ error.message() });
        }
    }

    // Ask the person a question, as Windows asks one: over the window,
    // with a line of text and the answers it is given.
    //
    // ARGS says the TITLE and the MESSAGE, and names the answers:
    // ACCEPT is the one it opens on, OTHER a second, and CANCEL the one
    // it closes on, which is also what pressing Escape comes to.  An
    // answer left unnamed is not offered.  Answers with "accept",
    // "other" or "cancel".
    fire_and_forget AskAsync(Window window, JsonObject args, Reply reply)
    {
        try
        {
            Controls::ContentDialog dialog;
            auto accept = args.GetNamedString(L"accept", L"");
            auto other = args.GetNamedString(L"other", L"");
            auto cancel = args.GetNamedString(L"cancel", L"");

            dialog.XamlRoot(window.Content().XamlRoot());
            dialog.Title(box_value(args.GetNamedString(L"title", L"")));
            dialog.Content(box_value(args.GetNamedString(L"message", L"")));
            if (!accept.empty())
            {
                dialog.PrimaryButtonText(accept);
                dialog.DefaultButton(Controls::ContentDialogButton::Primary);
            }
            if (!other.empty())
            {
                dialog.SecondaryButtonText(other);
            }
            if (!cancel.empty())
            {
                dialog.CloseButtonText(cancel);
            }

            auto chosen = co_await dialog.ShowAsync();
            reply(JsonValue::CreateStringValue(
                      chosen == Controls::ContentDialogResult::Primary     ? L"accept"
                      : chosen == Controls::ContentDialogResult::Secondary ? L"other"
                                                                          : L"cancel"),
                  L"");
        }
        catch (hresult_error const& error)
        {
            reply(JsonValue::CreateNullValue(), std::wstring{ error.message() });
        }
    }

    void Ask(Window const& window, JsonObject const& args, Reply const& reply)
    {
        AskAsync(window, args, reply);
    }

    void OpenFile(Window const& window, JsonObject const& args, Reply const& reply)
    {
        OpenFileAsync(window, args, reply);
    }

    std::map<std::wstring, Method> const& Methods()
    {
        static std::map<std::wstring, Method> const methods{
            { L"window.title", Title },
            { L"window.state", State },
            { L"window.topmost", Topmost },
            { L"window.size", Size },
            { L"window.resize", Resize },
            { L"window.theme", Theme },
            { L"dialog.open-file", OpenFile },
            { L"dialog.ask", Ask },
        };
        return methods;
    }
}

namespace urusi::windows::emacs
{
    void HostCalls::Call(Window const& window, std::wstring const& method,
                         JsonObject const& args, Reply reply)
    {
        auto found = Methods().find(method);
        if (found == Methods().end())
        {
            reply(JsonValue::CreateNullValue(), L"no such method: " + method);
            return;
        }

        try
        {
            found->second(window, args, reply);
        }
        catch (hresult_error const& error)
        {
            reply(JsonValue::CreateNullValue(), std::wstring{ error.message() });
        }
    }
}
