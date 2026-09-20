#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <cwchar>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Data::Json;

namespace
{
    constexpr uint16_t kDefaultPort = 7680;

    // The port can be overridden with URUSI_PORT, for running a second host
    // next to an installed one.
    uint16_t ReadPort()
    {
        wchar_t buffer[16]{};
        DWORD length = GetEnvironmentVariableW(L"URUSI_PORT", buffer, ARRAYSIZE(buffer));
        if (length == 0 || length >= ARRAYSIZE(buffer))
        {
            return kDefaultPort;
        }
        unsigned long value = wcstoul(buffer, nullptr, 10);
        return (value > 0 && value <= 65535) ? static_cast<uint16_t>(value) : kDefaultPort;
    }

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
        Start(ReadPort());
        StartEmacs();
    }

    void MainWindow::Start(uint16_t port)
    {
        m_dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        try
        {
            m_server = std::make_unique<urusi::UiServer>(
                port,
                [weak, dispatcher](std::string line) {
                    dispatcher.TryEnqueue([weak, line = std::move(line)] {
                        if (auto self = weak.get())
                        {
                            self->OnMessage(line);
                        }
                    });
                },
                [weak, dispatcher, port] {
                    dispatcher.TryEnqueue([weak, port] {
                        if (auto self = weak.get())
                        {
                            self->Surface().Children().Clear();
                            self->ShowStatus(L"Emacs disconnected. Waiting on 127.0.0.1:" + to_hstring(port));
                        }
                    });
                });
            ShowStatus(L"Waiting for Emacs on 127.0.0.1:" + to_hstring(port));
        }
        catch (std::exception const& e)
        {
            ShowStatus(L"Cannot listen on 127.0.0.1:" + to_hstring(port) + L": " + to_hstring(e.what()));
        }
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
            args.push_back("(urusi-connect)");
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
        if (m_server)
        {
            m_server->Send(to_string(message.Stringify()));
        }
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
