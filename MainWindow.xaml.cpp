#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// For the window handle of this window, which is what tells Emacs that
// there is a host here at all.
#include <microsoft.ui.xaml.window.h>

#include <algorithm>
#include <chrono>
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

    // Whether KEY is one the input method answers itself, rather than
    // one that means a character.
    //
    // These are the keys that turn it on and off and work its way
    // through a conversion, and the one Windows sends in place of a key
    // it has already handled. None of them is Emacs's to see, and
    // taking them here is what stops the input method being switched at
    // all.
    bool IsInputMethodKey(winrt::Windows::System::VirtualKey key)
    {
        switch (static_cast<int>(key))
        {
        case VK_KANA:           // and VK_HANGUL: the same number
        case VK_JUNJA:
        case VK_FINAL:
        case VK_KANJI:          // and VK_HANJA
        case VK_CONVERT:
        case VK_NONCONVERT:
        case VK_ACCEPT:
        case VK_MODECHANGE:
        case VK_PROCESSKEY:
        case VK_OEM_ATTN:
        case VK_OEM_AUTO:
        case VK_OEM_ENLW:
        case VK_OEM_BACKTAB:
            return true;
        default:
            return false;
        }
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

        // Emacs asks for this to know that a host is here, and makes
        // its frame a message-only window when one is. It is not a
        // parent: nothing of Emacs is ever on this window.
        urusi::HostApi::Instance().SetWindow(window);

        // Emacs lays its text out to the size of its frame, and the
        // frame is as big as the area the screen is drawn in.
        EditorSite().SizeChanged([weak](IInspectable const&, SizeChangedEventArgs const&) {
            if (auto self = weak.get())
            {
                self->SizeEmacsFrame();
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
        AppendLog("host", std::string{ "urusi-emacs built " } + __DATE__ + " " + __TIME__ + "\n");

        ShowStatus(L"Waiting for Emacs");
        ShowEventually();
    }

    void MainWindow::TakeEmacsWindow(HWND window)
    {
        m_emacsWindow = window;
        ShowStatus(L"");
        SizeEmacsFrame();
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
        // as well would type it twice. A key that works the input
        // method itself is not a key either, and is left alone
        // entirely: answering it here is what stops it turning the
        // input method on and off.
        if (args.Key() == Windows::System::VirtualKey::None
            || IsInputMethodKey(args.Key()))
        {
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

    // Emacs lays its text out to the size of its frame, so the frame is
    // told the size of the area the screen is drawn in.
    //
    // Told, rather than resized: the frame's window is on no screen and
    // its size means nothing to Windows, and a window message would
    // cost Emacs the thread it reads its input on, which a keystroke
    // then waits behind.
    void MainWindow::SizeEmacsFrame()
    {
        auto site = EditorSite();
        if (!site.XamlRoot())
        {
            return;
        }

        // XAML works in device-independent pixels and Emacs in the ones
        // of the screen.
        double scale = site.XamlRoot().RasterizationScale();
        auto pixels = [scale](double value) { return static_cast<int>(std::lround(value * scale)); };

        SIZE size{ pixels(site.ActualWidth()), pixels(site.ActualHeight()) };

        if (size.cx <= 0 || size.cy <= 0
            || (size.cx == m_emacsSize.cx && size.cy == m_emacsSize.cy))
        {
            return;
        }
        m_emacsSize = size;

        JsonObject message;
        message.SetNamedValue(L"type", String(L"resize"));
        message.SetNamedValue(L"width", JsonValue::CreateNumberValue(size.cx));
        message.SetNamedValue(L"height", JsonValue::CreateNumberValue(size.cy));
        Send(message);
    }

    void MainWindow::StartEmacs()
    {
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        // No -Q: this is the user's Emacs, and it reads the user's init
        // file like any other. Nothing is said here about the screen:
        // site-start.el brings urusi up before the init file, so that
        // the init file can say what the screen should look like, and
        // shows it once the init file has.
        std::vector<std::string> args{ "emacs" };

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

        auto family = message.GetNamedString(L"family", L"Consolas");
        double size = message.GetNamedNumber(L"size", 14);

        // One of each kind that Emacs counts differently: a character
        // of one column and one of two. Emacs says which font a run is
        // drawn in, so both are measured in the font asked about.
        auto advance = [&](wchar_t sample) {
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
        reply.SetNamedValue(L"family", String(family));
        reply.SetNamedValue(L"size", JsonValue::CreateNumberValue(size));
        reply.SetNamedValue(L"narrow", JsonValue::CreateNumberValue(advance(L'0')));
        // HIRAGANA LETTER A, spelled out: this file is read as bytes.
        reply.SetNamedValue(L"wide", JsonValue::CreateNumberValue(advance(L'\x3042')));
        Send(reply);
    }

    void MainWindow::Screen(JsonObject const& message)
    {
        ShowWhenReady();

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

    // Show the window, once there is something in it worth looking at.
    //
    // Emacs reads the user's init file before it has a screen to show,
    // and that can take seconds. An empty window for those seconds is
    // worse than no window, so there is none until the first screen
    // arrives -- or until it is clear that none is coming, because a
    // window saying what went wrong is the only way to find out that
    // anything did.
    void MainWindow::ShowWhenReady()
    {
        if (m_shown)
        {
            return;
        }
        m_shown = true;

        AppWindow().Show();
        Activate();
    }

    void MainWindow::ShowEventually()
    {
        constexpr std::chrono::seconds kPatience{ 5 };

        auto timer = m_dispatcher.CreateTimer();
        auto weak = get_weak();

        timer.Interval(kPatience);
        timer.IsRepeating(false);
        timer.Tick([weak](auto&& sender, auto&&) {
            sender.Stop();
            if (auto self = weak.get())
            {
                self->ShowWhenReady();
            }
        });
        timer.Start();
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
