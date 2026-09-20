#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// For the window handle of this window, which is what Emacs makes its
// frame a child of.
#include <microsoft.ui.xaml.window.h>

#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Data::Json;

namespace
{
    JsonValue String(hstring const& text)
    {
        return JsonValue::CreateStringValue(text);
    }

    // Enough of Emacs's output to see how it started, and no more.
    constexpr size_t kLogLimit = 16384;

    // Where urusi.el is: URUSI_LISP_DIR, or lisp next to the
    // application. Emacs takes its command line in the encoding of the
    // system, so this stays narrow all the way.
    std::string LispDirectory()
    {
        char configured[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableA("URUSI_LISP_DIR", configured, ARRAYSIZE(configured));
        if (length > 0 && length < ARRAYSIZE(configured))
        {
            return std::string{ configured, length };
        }

        char path[MAX_PATH]{};
        length = GetModuleFileNameA(nullptr, path, ARRAYSIZE(path));
        if (length == 0 || length == ARRAYSIZE(path))
        {
            return {};
        }

        std::string directory{ path, length };
        auto slash = directory.find_last_of('\\');
        return slash == std::string::npos ? std::string{} : directory.substr(0, slash) + "\\lisp";
    }
}

namespace winrt::urusi_emacs::implementation
{
    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();
        Start();
        StartEmacs();
    }

    void MainWindow::Start()
    {
        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        // Emacs posts from its own thread, and the window may only be
        // touched from this one.
        urusi::HostApi::Instance().OnMessage([weak, dispatcher](std::string message) {
            dispatcher.TryEnqueue([weak, message = std::move(message)] {
                if (auto self = weak.get())
                {
                    self->OnMessage(message);
                }
            });
        });
        HWND window = nullptr;
        check_hresult(try_as<::IWindowNative>()->get_WindowHandle(&window));
        urusi::HostApi::Instance().SetWindow(window);

        // The frame is placed over EditorSite and has to follow it.
        EditorSite().SizeChanged([weak](IInspectable const&, SizeChangedEventArgs const&) {
            if (auto self = weak.get())
            {
                self->PlaceEmacsWindow();
            }
        });

        ShowStatus(L"Waiting for Emacs");
    }

    void MainWindow::TakeEmacsWindow(HWND window)
    {
        m_emacsWindow = window;
        LogView().Visibility(Visibility::Collapsed);
        ShowStatus(L"");
        PlaceEmacsWindow();
    }

    void MainWindow::PlaceEmacsWindow()
    {
        if (!m_emacsWindow)
        {
            return;
        }

        auto site = EditorSite();
        auto root = Content();
        if (!site.XamlRoot() || !root)
        {
            return;
        }

        // XAML works in device-independent pixels and a window in
        // physical ones.
        double scale = site.XamlRoot().RasterizationScale();
        auto origin = site.TransformToVisual(root).TransformPoint({ 0, 0 });
        auto pixels = [scale](double value) { return static_cast<int>(std::lround(value * scale)); };

        // SWP_ASYNCWINDOWPOS: the window belongs to a thread of Emacs's,
        // and this one must not wait on it.
        SetWindowPos(m_emacsWindow, nullptr,
                     pixels(origin.X), pixels(origin.Y),
                     pixels(site.ActualWidth()), pixels(site.ActualHeight()),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    }

    void MainWindow::StartEmacs()
    {
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        std::vector<std::string> args{ "emacs", "-Q" };
        auto lisp = LispDirectory();
        if (!lisp.empty())
        {
            args.push_back("-l");
            args.push_back(lisp + "\\urusi.el");
            args.push_back("--eval");
            args.push_back("(urusi-start)");
        }

        std::string error;
        bool started = urusi::EmacsHost::Instance().Start(
            urusi::EmacsHost::DefaultDll(),
            args,
            [weak, dispatcher](std::string text) {
                dispatcher.TryEnqueue([weak, text = std::move(text)] {
                    if (auto self = weak.get())
                    {
                        self->AppendLog(text);
                    }
                });
            },
            error);
        if (!started)
        {
            AppendLog(error + "\n");
        }
    }

    void MainWindow::OnMessage(std::string const& line)
    {
        JsonObject message{ nullptr };
        if (!JsonObject::TryParse(to_hstring(line), message))
        {
            SendError(L"invalid JSON");
            return;
        }

        auto type = message.GetNamedString(L"type", L"");
        if (type == L"hello")
        {
            ShowStatus(L"");
            JsonObject reply;
            reply.SetNamedValue(L"type", String(L"hello"));
            reply.SetNamedValue(L"host", String(L"urusi-emacs"));
            reply.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
            Send(reply);
        }
        else if (type == L"render")
        {
            Render(message);
        }
        else if (type == L"frame")
        {
            TakeEmacsWindow(reinterpret_cast<HWND>(
                static_cast<INT_PTR>(message.GetNamedNumber(L"window", 0))));
        }
        else
        {
            SendError(L"unknown message type: " + type);
        }
    }

    void MainWindow::Render(JsonObject const& message)
    {
        UIElement root{ nullptr };
        try
        {
            root = Markup::XamlReader::Load(message.GetNamedString(L"xaml", L"")).as<UIElement>();
        }
        catch (hresult_error const& e)
        {
            SendError(L"XAML: " + e.message());
            return;
        }

        Surface().Children().Clear();
        Surface().Children().Append(root);
        ShowStatus(L"");

        if (auto element = root.try_as<FrameworkElement>())
        {
            AttachEvents(element, message.GetNamedArray(L"events", JsonArray{}));
        }
    }

    void MainWindow::AttachEvents(FrameworkElement const& root, JsonArray const& events)
    {
        using namespace Microsoft::UI::Xaml::Controls;

        for (auto const& value : events)
        {
            auto entry = value.GetObject();
            auto name = entry.GetNamedString(L"name", L"");
            auto event = entry.GetNamedString(L"event", L"");
            auto id = static_cast<int64_t>(entry.GetNamedNumber(L"id", -1));

            auto target = root.FindName(name);
            if (!target)
            {
                SendError(L"no element named " + name);
                continue;
            }

            auto weak = get_weak();
            if (event == L"Click")
            {
                if (auto button = target.try_as<Primitives::ButtonBase>())
                {
                    button.Click([weak, id](IInspectable const&, RoutedEventArgs const&) {
                        if (auto self = weak.get())
                        {
                            self->SendEvent(id, JsonObject{});
                        }
                    });
                    continue;
                }
            }
            else if (event == L"TextChanged")
            {
                if (auto box = target.try_as<TextBox>())
                {
                    box.TextChanged([weak, id](IInspectable const& sender, TextChangedEventArgs const&) {
                        if (auto self = weak.get())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"text", String(sender.as<TextBox>().Text()));
                            self->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }
            else if (event == L"SelectionChanged")
            {
                if (auto selector = target.try_as<Primitives::Selector>())
                {
                    selector.SelectionChanged([weak, id](IInspectable const& sender, SelectionChangedEventArgs const&) {
                        if (auto self = weak.get())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"index", JsonValue::CreateNumberValue(
                                sender.as<Primitives::Selector>().SelectedIndex()));
                            self->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }
            else if (event == L"Toggled")
            {
                if (auto toggle = target.try_as<ToggleSwitch>())
                {
                    toggle.Toggled([weak, id](IInspectable const& sender, RoutedEventArgs const&) {
                        if (auto self = weak.get())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"on", JsonValue::CreateBooleanValue(sender.as<ToggleSwitch>().IsOn()));
                            self->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }

            SendError(L"cannot attach " + event + L" to " + name);
        }
    }

    void MainWindow::SendEvent(int64_t id, JsonObject const& args)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String(L"event"));
        message.SetNamedValue(L"id", JsonValue::CreateNumberValue(static_cast<double>(id)));
        message.SetNamedValue(L"args", args);
        Send(message);
    }

    void MainWindow::SendError(hstring const& text)
    {
        ShowStatus(text);
        JsonObject message;
        message.SetNamedValue(L"type", String(L"error"));
        message.SetNamedValue(L"message", String(text));
        Send(message);
    }

    void MainWindow::Send(JsonObject const& message)
    {
        urusi::HostApi::Instance().Send(to_string(message.Stringify()));
    }

    void MainWindow::ShowStatus(hstring const& text)
    {
        Status().Text(text);
    }

    void MainWindow::AppendLog(std::string const& text)
    {
        m_log += text;
        if (m_log.size() > kLogLimit)
        {
            m_log.erase(0, m_log.size() - kLogLimit);
        }
        Log().Text(to_hstring(m_log));

        // The extent only grows once the new text has been laid out.
        LogView().UpdateLayout();
        LogView().ChangeView(nullptr, LogView().ScrollableHeight(), nullptr);
    }
}
