#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// For the window handle of this window, which is what Emacs makes its
// frame a child of.
#include <microsoft.ui.xaml.window.h>

#include <algorithm>
#include <limits>
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

    // The log, again, where it can be read without a debugger: beside
    // the application, emptied when it starts, so that what is in it is
    // this run and not the last one.
    void WriteToLogFile(std::string const& line)
    {
        static HANDLE file = [] {
            char path[MAX_PATH]{};
            DWORD length = GetModuleFileNameA(nullptr, path, ARRAYSIZE(path));
            if (length == 0 || length == ARRAYSIZE(path))
            {
                return INVALID_HANDLE_VALUE;
            }

            std::string name{ path, length };
            auto slash = name.find_last_of('\\');
            if (slash == std::string::npos)
            {
                return INVALID_HANDLE_VALUE;
            }

            // FILE_SHARE_READ, or nothing may look at it while it runs,
            // which is the only time it is worth looking at.
            return CreateFileA((name.substr(0, slash) + "\\urusi-emacs.log").c_str(),
                               GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }();

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        FlushFileBuffers(file);
    }

    // The window the Emacs frame lives in.
    //
    // Emacs draws its frame as it always has, and nobody sees it: what
    // is seen is built in Lisp and drawn by XAML. The frame cannot be a
    // child of the window that shows, because XAML draws into a layered
    // window and leaves every part it did not paint clear, and the
    // frame shows through those parts. So the frame is given a
    // top-level window of its own that is never shown, which also puts
    // it out of reach of the mouse and out of the way of the focus.
    HWND MakeOffscreenHolder()
    {
        static ATOM registered = [] {
            WNDCLASSEXW description{ sizeof(description) };

            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = GetModuleHandleW(nullptr);
            description.lpszClassName = L"urusi-emacs-offscreen";
            return RegisterClassExW(&description);
        }();

        if (!registered)
        {
            return nullptr;
        }

        // Not WS_VISIBLE: the frame inside it is shown, and so counts
        // as visible to Emacs, which will not lay out a frame it
        // believes nobody is looking at.
        return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               L"urusi-emacs-offscreen", L"",
                               WS_POPUP | WS_CLIPCHILDREN,
                               0, 0, 1, 1,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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
        m_holder = MakeOffscreenHolder();
        urusi::HostApi::Instance().SetWindow(m_holder);

        // The frame is sized to EditorSite and has to follow it.
        EditorSite().SizeChanged([weak](IInspectable const&, SizeChangedEventArgs const&) {
            if (auto self = weak.get())
            {
                self->SizeEmacsWindow();
            }
        });

        // Emacs reads the modifier keys from the input queue of the
        // thread its frame belongs to, so this thread's queue has to be
        // joined to it again whenever the window comes back.
        Activated([weak](IInspectable const&, WindowActivatedEventArgs const& args) {
            if (auto self = weak.get();
                self && args.WindowActivationState() != WindowActivationState::Deactivated)
            {
                self->TakeInputToEmacs();
            }
        });

        if (auto content = Content().try_as<UIElement>())
        {
            // What XAML sees, Emacs gets.
            content.KeyDown([weak](IInspectable const&, Input::KeyRoutedEventArgs const& args) {
                if (auto self = weak.get())
                {
                    self->ForwardKey(args, true);
                }
            });
            content.KeyUp([weak](IInspectable const&, Input::KeyRoutedEventArgs const& args) {
                if (auto self = weak.get())
                {
                    self->ForwardKey(args, false);
                }
            });
        }

        StartComposition();

        // Which build this is, so that a stale one is obvious.
        AppendLog("host", std::string{ "urusi-emacs built " } + __DATE__ + " " + __TIME__
                  + (m_holder ? "\n" : ", no offscreen holder: the frame will show\n"));

        ShowStatus(L"Waiting for Emacs");
    }

    void MainWindow::TakeEmacsWindow(HWND window)
    {
        m_emacsWindow = window;
        ShowStatus(L"");
        SizeEmacsWindow();
        TakeInputToEmacs();
    }

    void MainWindow::TakeInputToEmacs()
    {
        if (!m_emacsWindow)
        {
            return;
        }

        // The frame window belongs to a thread of Emacs's own, and a
        // thread may only give the focus to a window on its own input
        // queue. Joining the two queues lets this one hand the focus
        // over, and lets Emacs read the modifier keys as they really
        // are, which is what its own key handling asks the system for.
        DWORD emacs = GetWindowThreadProcessId(m_emacsWindow, nullptr);
        BOOL attached = TRUE;

        if (emacs && emacs != GetCurrentThreadId() && !m_attached)
        {
            attached = AttachThreadInput(GetCurrentThreadId(), emacs, TRUE);
            m_attached = attached != FALSE;
        }

        // The focus stays with XAML, which is where the input method
        // talks; the keys are passed on from there. A frame that took
        // the focus would take the input method with it, into a window
        // that cannot be seen.

        if (m_seen.insert(L"focus").second)
        {
            HWND focus = GetFocus();
            wchar_t name[64]{};

            if (focus)
            {
                GetClassNameW(focus, name, ARRAYSIZE(name));
            }
            AppendLog("host", "attach " + std::to_string(attached) + ", focus "
                      + std::to_string(reinterpret_cast<INT_PTR>(focus)) + " ("
                      + to_string(hstring{ name }) + "), frame "
                      + std::to_string(reinterpret_cast<INT_PTR>(m_emacsWindow)) + "\n");
        }
    }

    // Let the input method have the keys first, and give Emacs what it
    // makes of them. Everything it does not want arrives as a key, as
    // before.
    void MainWindow::StartComposition()
    {
        auto weak = get_weak();

        m_composition.Trace([weak](std::string what) {
            if (auto self = weak.get())
            {
                self->AppendLog("ime", what);
            }
        });
        m_composition.Start(
            InputSink(),
            [weak](std::wstring text) {
                if (auto self = weak.get())
                {
                    self->TypeIntoEmacs(text);
                }
            },
            [weak](std::wstring text) {
                if (auto self = weak.get())
                {
                    JsonObject message;
                    message.SetNamedValue(L"type", String(L"composition"));
                    message.SetNamedValue(L"text", String(hstring{ text }));
                    self->Send(message);
                }
            });

        InputSink().Focus(FocusState::Programmatic);
    }

    // Put TEXT into Emacs as the characters it is. Emacs reads them
    // the way it reads anything typed, so whatever is bound to them
    // runs.
    void MainWindow::TypeIntoEmacs(std::wstring const& text)
    {
        if (!m_emacsWindow)
        {
            return;
        }

        AppendLog("host", "type \"" + to_string(hstring{ text }) + "\"\n");

        for (wchar_t character : text)
        {
            PostMessageW(m_emacsWindow, WM_CHAR, static_cast<WPARAM>(character), 1);
        }
    }

    // Where Emacs says the caret is, in the pixels of the screen, so
    // that the candidates appear beside it.
    void MainWindow::Caret(JsonObject const& message)
    {
        HWND window = nullptr;
        auto site = EditorSite();

        if (!site.XamlRoot()
            || FAILED(try_as<::IWindowNative>()->get_WindowHandle(&window)))
        {
            return;
        }

        // Emacs counts from the corner of the area it was given, in
        // the pixels of the screen; this wants the corner of the
        // screen, in the 96ths of an inch XAML counts in.
        double scale = site.XamlRoot().RasterizationScale();
        auto corner = site.TransformToVisual(Content()).TransformPoint({ 0, 0 });
        POINT client{ 0, 0 };

        ClientToScreen(window, &client);

        m_composition.SetCaret({
            static_cast<float>(client.x / scale + corner.X + message.GetNamedNumber(L"x", 0) / scale),
            static_cast<float>(client.y / scale + corner.Y + message.GetNamedNumber(L"y", 0) / scale),
            static_cast<float>(message.GetNamedNumber(L"width", 2) / scale),
            static_cast<float>(message.GetNamedNumber(L"height", 16) / scale) });
    }

    // Give ARGS to the Emacs frame as the key message it was, and let
    // Emacs make of it what it makes of any other. Only the key goes:
    // Emacs turns it into a character itself, from the state of the
    // keyboard, which is this thread's as well now that the two input
    // queues are one.
    void MainWindow::ForwardKey(Input::KeyRoutedEventArgs const& args, bool down)
    {
        if (!m_emacsWindow)
        {
            return;
        }

        // A key the input method is making something of is not a key:
        // what it settles on arrives as text, and passing the key on
        // as well would type it twice.
        if (args.Key() == Windows::System::VirtualKey::None
            || static_cast<int>(args.Key()) == 229)
        {
            args.Handled(true);
            return;
        }

        auto status = args.KeyStatus();
        LPARAM extra = static_cast<LPARAM>(status.RepeatCount)
            | (static_cast<LPARAM>(status.ScanCode) << 16)
            | (status.IsExtendedKey ? (1LL << 24) : 0)
            | (status.IsMenuKeyDown ? (1LL << 29) : 0)
            | (status.WasKeyDown ? (1LL << 30) : 0)
            | (down ? 0 : (1LL << 31));

        // Alt held is a system key to Windows and the meta key to
        // Emacs, and it arrives under another name.
        UINT message = status.IsMenuKeyDown
            ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP)
            : (down ? WM_KEYDOWN : WM_KEYUP);

        if (down)
        {
            AppendLog("host", "key " + std::to_string(static_cast<int>(args.Key())) + "\n");
        }
        PostMessageW(m_emacsWindow, message,
                     static_cast<WPARAM>(args.Key()), extra);
        args.Handled(true);
    }

    // The frame is never seen, but its size is still what Emacs lays
    // its text out to, so it is kept the size of the area the screen is
    // drawn in.
    void MainWindow::SizeEmacsWindow()
    {
        if (!m_emacsWindow)
        {
            return;
        }

        auto site = EditorSite();
        if (!site.XamlRoot())
        {
            return;
        }

        // XAML works in device-independent pixels and a window in
        // physical ones.
        double scale = site.XamlRoot().RasterizationScale();
        auto pixels = [scale](double value) { return static_cast<int>(std::lround(value * scale)); };

        int width = pixels(site.ActualWidth());
        int height = pixels(site.ActualHeight());

        if (m_holder)
        {
            SetWindowPos(m_holder, nullptr, 0, 0, width, height,
                         SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
        }

        // SWP_SHOWWINDOW: the frame is shown inside a window that is
        // not, which is how Emacs comes to believe it is looked at.
        // SWP_ASYNCWINDOWPOS: the frame belongs to a thread of Emacs's,
        // and this thread must not wait on it.
        SetWindowPos(m_emacsWindow, nullptr, 0, 0, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    }

    void MainWindow::StartEmacs()
    {
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        std::vector<std::string> args{ "emacs", "-Q" };
        auto lisp = LispDirectory();
        if (!lisp.empty())
        {
            // Emacs loads the Lisp itself, inside a condition-case, so
            // that a failure comes back here rather than going to a
            // standard error that nothing reads. The directory goes on
            // the load path, because one file there requires another.
            // Lisp takes the path with forward slashes, which spares
            // the escaping.
            std::string path = lisp;
            std::replace(path.begin(), path.end(), '\\', '/');

            args.push_back("--eval");
            args.push_back("(condition-case error"
                           " (progn (add-to-list 'load-path \"" + path + "\")"
                           " (require 'urusi-screen)"
                           " (urusi-start) (urusi-screen-mode 1))"
                           " (error (w32-host-post (json-serialize"
                           " (list :type \"log\""
                           " :text (format \"startup: %S\" error))))))");
        }

        std::string error;
        bool started = urusi::EmacsHost::Instance().Start(
            urusi::EmacsHost::DefaultDll(),
            args,
            [weak, dispatcher](std::string text) {
                dispatcher.TryEnqueue([weak, text = std::move(text)] {
                    if (auto self = weak.get())
                    {
                        self->AppendLog("emacs", text);
                    }
                });
            },
            error);
        if (!started)
        {
            AppendLog("host", error + "\n");
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

        // The first of each kind, so that a message that never comes is
        // as plain to see as one that fails.
        if (m_seen.insert(std::wstring{ type }).second)
        {
            AppendLog("host", "first " + to_string(type) + ", " + std::to_string(line.size())
                      + " bytes\n");
        }

        if (type == L"hello")
        {
            ShowStatus(L"");
            JsonObject reply;
            reply.SetNamedValue(L"type", String(L"hello"));
            reply.SetNamedValue(L"host", String(L"urusi-emacs"));
            reply.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
            // Emacs measures in the pixels of the screen and XAML in
            // 96ths of an inch, and this is what lies between them.
            reply.SetNamedValue(L"scale", JsonValue::CreateNumberValue(
                Content() && Content().XamlRoot() ? Content().XamlRoot().RasterizationScale() : 1.0));
            Send(reply);
        }
        else if (type == L"screen")
        {
            Screen(message);
        }
        else if (type == L"caret")
        {
            Caret(message);
        }
        else if (type == L"measure")
        {
            Measure(message);
        }
        else if (type == L"log")
        {
            AppendLog("emacs", to_string(message.GetNamedString(L"text", L"")) + "\n");
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

    // Say how wide a character of a font is here. Emacs lays its text
    // out on a grid of whole pixels and XAML does not, so the two
    // drift apart across a line unless Emacs is told what to correct
    // for.
    void MainWindow::Measure(JsonObject const& message)
    {
        constexpr int kSample = 100;

        auto narrow = message.GetNamedString(L"family", L"Consolas");
        auto wide = message.GetNamedString(L"wide-family", narrow);
        double size = message.GetNamedNumber(L"size", 14);

        // One of each kind that Emacs counts differently: a character
        // of one column and one of two, each in the font it is drawn
        // in.
        auto advance = [&](hstring const& family, wchar_t sample) {
            Controls::TextBlock block;
            block.FontFamily(Media::FontFamily{ family });
            block.FontSize(size);
            block.Text(hstring{ std::wstring(kSample, sample) });
            block.Measure({ std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity() });
            return block.DesiredSize().Width / kSample;
        };

        JsonObject reply;
        reply.SetNamedValue(L"type", String(L"measured"));
        reply.SetNamedValue(L"narrow", JsonValue::CreateNumberValue(advance(narrow, L'0')));
        // HIRAGANA LETTER A, spelled out: this file is read as bytes.
        reply.SetNamedValue(L"wide", JsonValue::CreateNumberValue(advance(wide, L'\x3042')));
        Send(reply);
    }

    void MainWindow::Screen(JsonObject const& message)
    {
        // Emacs sets its frame to a size of its own during startup, and
        // whenever Lisp asks it to. Where the frame goes is this
        // window's to say, so say it again.
        SizeEmacsWindow();

        // The XAML around the rows comes only when it has changed, and
        // everything in it goes with it.
        if (message.HasKey(L"xaml"))
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

        auto root = Surface().Children().Size() ? Surface().Children().GetAt(0) : nullptr;
        auto named = root ? root.try_as<FrameworkElement>() : nullptr;

        for (auto const& value : message.GetNamedArray(L"rows", JsonArray{}))
        {
            auto group = value.GetObject();
            auto name = group.GetNamedString(L"panel", L"");
            auto target = named ? named.FindName(name) : nullptr;

            if (auto panel = target ? target.try_as<Controls::Panel>() : nullptr)
            {
                ReconcileRows(panel, group.GetNamedArray(L"items", JsonArray{}));
            }
            else
            {
                SendError(L"no panel named " + name);
            }
        }
    }

    // Put the rows of PANEL in the order ITEMS gives, building the ones
    // that came with XAML of their own and keeping the ones that did
    // not. A row is known by its key, which it carries in its Tag.
    void MainWindow::ReconcileRows(Controls::Panel const& panel, JsonArray const& items)
    {
        auto children = panel.Children();
        std::vector<UIElement> wanted;
        wanted.reserve(items.Size());

        for (auto const& value : items)
        {
            auto item = value.GetObject();
            auto key = item.GetNamedString(L"key", L"");

            if (item.HasKey(L"xaml"))
            {
                UIElement row{ nullptr };
                try
                {
                    row = Markup::XamlReader::Load(item.GetNamedString(L"xaml", L"")).as<UIElement>();
                }
                catch (hresult_error const& e)
                {
                    // One row that will not parse is one row missing,
                    // not a screen lost.
                    SendError(L"row " + key + L": " + e.message());
                    continue;
                }
                row.as<FrameworkElement>().Tag(box_value(key));
                wanted.push_back(row);
            }
            else
            {
                // Kept from last time, and found by its key.
                UIElement existing{ nullptr };
                for (uint32_t i = 0; i < children.Size(); i++)
                {
                    auto child = children.GetAt(i).try_as<FrameworkElement>();
                    if (child && unbox_value_or<hstring>(child.Tag(), L"") == key)
                    {
                        existing = child;
                        break;
                    }
                }

                if (!existing)
                {
                    // Emacs thinks this window shows something it does
                    // not. Ask for the whole screen again.
                    JsonObject stale;
                    stale.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stale"));
                    Send(stale);
                    return;
                }
                wanted.push_back(existing);
            }
        }

        // Move what is out of place, rather than taking the lot apart:
        // a row that is only further down the screen than it was keeps
        // whatever it was doing.
        for (uint32_t i = 0; i < wanted.size(); i++)
        {
            if (i < children.Size() && children.GetAt(i) == wanted[i])
            {
                continue;
            }

            uint32_t found = 0;
            if (children.IndexOf(wanted[i], found))
            {
                children.RemoveAt(found);
            }
            children.InsertAt(i, wanted[i]);
        }

        while (children.Size() > wanted.size())
        {
            children.RemoveAtEnd();
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
        AppendLog("host", "error: " + to_string(text) + "\n");
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

    void MainWindow::AppendLog(char const* source, std::string const& text)
    {
        std::string line = std::string{ source } + "| " + text;

        // The screen covers the log once Emacs draws one, and the
        // debugger is where anyone looking for this will be.
        OutputDebugStringA(line.c_str());
        WriteToLogFile(line);

        m_log += line;
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
