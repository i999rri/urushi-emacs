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

        // Started under a debugger, as from Visual Studio, everything is
        // written down from the first key: whatever is being chased may
        // happen before there is a chance to ask for it.
        m_debug = IsDebuggerPresent() != FALSE;
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
        m_window = window;

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
            auto self = weak.get();
            if (!self)
            {
                return;
            }

            bool active = args.WindowActivationState() != WindowActivationState::Deactivated;
            self->TraceFocus(active ? "activated" : "deactivated");
            if (active)
            {
                self->TakeInputToEmacs();
                self->KeepFocus();

                // Only on coming back from another window. The window is
                // also told it is active while it already is, as the
                // input method's own windows come and go, and telling
                // the input method again then ends what it is composing.
                if (!self->m_active)
                {
                    self->ResumeComposition();
                }
            }
            else
            {
                // Told to have gone while still in front, as when a
                // window of the input method comes and goes, and not
                // told to have come back after. So it is believed only
                // once Windows agrees, after what XAML is doing about it
                // has been done.
                self->m_dispatcher.TryEnqueue(
                    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                    [weak] {
                        auto self = weak.get();
                        if (!self)
                        {
                            return;
                        }

                        self->TraceFocus("deactivated, checked");
                        if (self->IsForeground())
                        {
                            return;
                        }

                        // Said to have gone, so that coming back is news
                        // to it: told only that the focus is here, while
                        // it thinks it never left, it goes on talking to
                        // the window it talked to meanwhile.
                        self->m_composition.Focus(false);
                        self->TellEmacsFocus(false);
                        self->SendHostEvent(L"deactivated", JsonObject{});
                    });
                return;
            }
            self->TellEmacsFocus(true);
            self->SendHostEvent(L"activated", JsonObject{});
        });

        // Closing the window is Emacs's to decide, the way leaving Emacs
        // always has been: it asks about the buffers that are not saved
        // first, and may be told not to. Until Emacs can answer, or once
        // it has gone quiet since it was asked, the window closes as any
        // other window would, since one that cannot be closed is worse.
        AppWindow().Closing([weak](Microsoft::UI::Windowing::AppWindow const&,
                                   Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
            constexpr std::chrono::seconds kPatience{ 5 };

            auto self = weak.get();
            if (!self || !self->m_emacsReady)
            {
                return;
            }

            auto now = std::chrono::steady_clock::now();
            bool asked = self->m_closeAsked != std::chrono::steady_clock::time_point{};
            bool silent = asked && self->m_lastHeard < self->m_closeAsked
                && now - self->m_closeAsked > kPatience;
            if (silent)
            {
                return;
            }

            args.Cancel(true);
            self->m_closeAsked = now;
            self->SendHostEvent(L"close", JsonObject{});
        });

        // How the window takes up the screen can change without Lisp
        // asking, from its title bar or from Windows, and what Lisp draws
        // on it may depend on it: a maximize button that restores once
        // the window is maximized.
        AppWindow().Changed([weak](Microsoft::UI::Windowing::AppWindow const& sender,
                                   Microsoft::UI::Windowing::AppWindowChangedEventArgs const& args) {
            auto self = weak.get();
            if (!self || !(args.DidPresenterChange() || args.DidSizeChange()))
            {
                return;
            }

            std::wstring state = L"normal";
            if (sender.Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen)
            {
                state = L"fullscreen";
            }
            else if (auto overlapped = sender.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
            {
                switch (overlapped.State())
                {
                case Microsoft::UI::Windowing::OverlappedPresenterState::Maximized: state = L"maximized"; break;
                case Microsoft::UI::Windowing::OverlappedPresenterState::Minimized: state = L"minimized"; break;
                default: break;
                }
            }

            if (state != self->m_windowState)
            {
                self->m_windowState = state;
                JsonObject details;
                details.SetNamedValue(L"state", String(hstring{ state }));
                self->SendHostEvent(L"state", details);
            }
        });

        // Windows can be switched between light and dark while the
        // application runs, and what Lisp draws was chosen for one of
        // them.
        if (auto root = Content().try_as<FrameworkElement>())
        {
            root.ActualThemeChanged([weak](FrameworkElement const& sender, IInspectable const&) {
                if (auto self = weak.get())
                {
                    JsonObject details;
                    details.SetNamedValue(L"dark", JsonValue::CreateBooleanValue(
                        sender.ActualTheme() == ElementTheme::Dark));
                    self->SendHostEvent(L"theme", details);
                }
            });
        }

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

        // The window may have come to the front before there was a
        // frame to tell.
        if (m_active)
        {
            TellEmacsFocus(true);
        }
    }

    // Tell Emacs that its frame has the focus, or has lost it, when
    // this window does. Emacs learns it from the frame's window being
    // given the focus and having it taken away, and the frame's window
    // is one that is never shown, so it is never given anything: the
    // same messages are sent to it instead, and Emacs does with them
    // what it does for any frame, from how the cursor is drawn to
    // running the hooks that wait for the focus to change.
    void MainWindow::TellEmacsFocus(bool focused)
    {
        m_active = focused;
        if (m_emacsWindow)
        {
            PostMessageW(m_emacsWindow, focused ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
        }
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
            if (auto self = weak.get(); self && self->m_debug)
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
        auto site = FrameSite();

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

        if (down && m_debug)
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
    // The element of what Lisp built that is named NAME, or null.
    FrameworkElement MainWindow::Named(hstring const& name)
    {
        if (Surface().Children().Size() == 0)
        {
            return nullptr;
        }

        auto root = Surface().Children().GetAt(0).try_as<FrameworkElement>();
        auto found = root ? root.FindName(name) : nullptr;
        return found ? found.try_as<FrameworkElement>() : nullptr;
    }

    // Where the Emacs frame goes, whose size is the frame's: the element
    // Lisp named urusi-frame, or the whole window if it named none. What
    // Lisp puts around the frame, a title bar or a panel beside it, is
    // room the frame does not have.
    FrameworkElement MainWindow::FrameSite()
    {
        if (auto site = Named(L"urusi-frame"))
        {
            return site;
        }
        return EditorSite();
    }

    // Keep the frame the size of where Lisp put it, and the title bar
    // where Lisp drew it, as the window is resized.
    //
    // Whether the window has a title bar of its own follows from the
    // same thing: if Lisp drew one, the window's own is taken away, and
    // if it drew none the window keeps the one it has. The border stays
    // either way, since it is what the window is resized by.
    void MainWindow::FollowLayout()
    {
        auto weak = get_weak();
        auto titlebar = Named(L"urusi-titlebar");

        if (auto overlapped = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
            overlapped && overlapped.HasTitleBar() == (titlebar != nullptr))
        {
            overlapped.SetBorderAndTitleBar(true, titlebar == nullptr);
        }

        auto changed = [weak](IInspectable const&, SizeChangedEventArgs const&) {
            if (auto self = weak.get())
            {
                self->SizeEmacsFrame();
                self->UpdateTitleBarRegions();
            }
        };

        if (auto site = Named(L"urusi-frame"))
        {
            site.SizeChanged(changed);
            AttachMouse(site, nullptr);
        }

        // The frame of a panel says which window is its in its Tag, as
        // the number of the window, which is what its mouse is sent to.
        for (auto const& panel : PanelSites())
        {
            std::wstring id = std::wstring{ panel.Name() }.substr(12);
            panel.SizeChanged([weak, id](IInspectable const& sender, SizeChangedEventArgs const&) {
                if (auto self = weak.get())
                {
                    self->SizeFrame(sender.as<FrameworkElement>(), id);
                }
            });

            HWND frame = nullptr;
            if (auto tag = panel.Tag().try_as<hstring>())
            {
                frame = reinterpret_cast<HWND>(static_cast<INT_PTR>(std::wcstoll(tag->c_str(), nullptr, 10)));
            }
            AttachMouse(panel, frame);
        }
        if (titlebar)
        {
            titlebar.SizeChanged(changed);
        }
        UpdateTitleBarRegions();

        if (Surface().Children().Size())
        {
            AttachSplitters(Surface().Children().GetAt(0));
        }
    }

    // Pass the mouse on to Emacs over SITE, the element the frame is
    // shown in, as the messages Windows would have sent the frame's own
    // window: the buttons, the wheel, and where the pointer is, counted
    // from the corner of the frame in the pixels of the screen. Emacs
    // makes of them what it makes of any mouse, a click that moves the
    // point, a drag that selects, a wheel that scrolls.
    //
    // The pointer is captured while a button is down, so that a drag
    // that leaves the frame is followed to where it ends.
    void MainWindow::AttachMouse(FrameworkElement const& site, HWND frame)
    {
        auto weak = get_weak();

        // The window of the frame shown in SITE, or of the frame the
        // window shows if SITE is its.
        auto target = [weak, frame]() -> HWND {
            auto self = weak.get();
            return frame ? frame : (self ? self->m_emacsWindow : nullptr);
        };

        // What Emacs is told about a pointer: where it is on the frame,
        // and which buttons and modifier keys are down, as the flags of
        // a mouse message carry them.
        auto describe = [site](Input::PointerRoutedEventArgs const& args) {
            auto point = args.GetCurrentPoint(site);
            double scale = site.XamlRoot() ? site.XamlRoot().RasterizationScale() : 1.0;
            int x = static_cast<int>(std::lround(point.Position().X * scale));
            int y = static_cast<int>(std::lround(point.Position().Y * scale));

            auto properties = point.Properties();
            auto modifiers = args.KeyModifiers();
            WPARAM flags = 0;
            if (properties.IsLeftButtonPressed()) flags |= MK_LBUTTON;
            if (properties.IsRightButtonPressed()) flags |= MK_RBUTTON;
            if (properties.IsMiddleButtonPressed()) flags |= MK_MBUTTON;
            if ((modifiers & Windows::System::VirtualKeyModifiers::Shift) != Windows::System::VirtualKeyModifiers::None) flags |= MK_SHIFT;
            if ((modifiers & Windows::System::VirtualKeyModifiers::Control) != Windows::System::VirtualKeyModifiers::None) flags |= MK_CONTROL;

            return std::make_tuple(flags, MAKELPARAM(x, y), properties);
        };

        site.PointerPressed([weak, site, describe, target](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            auto self = weak.get();
            if (!self || !target())
            {
                return;
            }

            auto [flags, where, properties] = describe(args);
            UINT message = 0;
            switch (properties.PointerUpdateKind())
            {
            case Microsoft::UI::Input::PointerUpdateKind::LeftButtonPressed: message = WM_LBUTTONDOWN; break;
            case Microsoft::UI::Input::PointerUpdateKind::RightButtonPressed: message = WM_RBUTTONDOWN; break;
            case Microsoft::UI::Input::PointerUpdateKind::MiddleButtonPressed: message = WM_MBUTTONDOWN; break;
            default: return;
            }

            site.CapturePointer(args.Pointer());
            PostMessageW(target(), message, flags, where);
            args.Handled(true);
        });

        site.PointerReleased([weak, site, describe, target](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            auto self = weak.get();
            if (!self || !target())
            {
                return;
            }

            auto [flags, where, properties] = describe(args);
            UINT message = 0;
            switch (properties.PointerUpdateKind())
            {
            case Microsoft::UI::Input::PointerUpdateKind::LeftButtonReleased: message = WM_LBUTTONUP; break;
            case Microsoft::UI::Input::PointerUpdateKind::RightButtonReleased: message = WM_RBUTTONUP; break;
            case Microsoft::UI::Input::PointerUpdateKind::MiddleButtonReleased: message = WM_MBUTTONUP; break;
            default: return;
            }

            PostMessageW(target(), message, flags, where);
            if (!(flags & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            {
                site.ReleasePointerCapture(args.Pointer());
            }
            args.Handled(true);
        });

        site.PointerMoved([weak, describe, target](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            auto self = weak.get();
            if (!self || !target())
            {
                return;
            }

            auto [flags, where, properties] = describe(args);
            PostMessageW(target(), WM_MOUSEMOVE, flags, where);
        });

        // A wheel message says where the pointer is on the screen rather
        // than on the window, and Emacs turns it into a place on the
        // frame from its own window. The frame's window is on no screen
        // and is at its corner, so the place on the frame is what is
        // given.
        site.PointerWheelChanged([weak, describe, target](IInspectable const&, Input::PointerRoutedEventArgs const& args) {
            auto self = weak.get();
            if (!self || !target())
            {
                return;
            }

            auto [flags, where, properties] = describe(args);
            UINT message = properties.IsHorizontalMouseWheel() ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
            WPARAM wheel = MAKEWPARAM(static_cast<WORD>(flags),
                                      static_cast<WORD>(static_cast<short>(properties.MouseWheelDelta())));
            PostMessageW(target(), message, wheel, where);
            args.Handled(true);
        });
    }

    // Let the splitters Lisp put between the parts of a layout be
    // dragged. A splitter is named urusi-splitter:DIRECTION:BEFORE:AFTER,
    // and sits in a cell of its own between the cells of the parts
    // either side of it, in the grid they are in. Dragging it makes the
    // part with a size of its own bigger or smaller and leaves the one
    // that shares what is left to take the rest; if neither has one,
    // the one before is given one. Where it is let go is Lisp's to
    // remember, and it is told.
    void MainWindow::AttachSplitters(UIElement const& root)
    {
        constexpr double kMinimumPart = 40;

        struct Drag
        {
            bool active{ false };
            double start{ 0 };
            double before{ 0 };
            double after{ 0 };
        };

        auto weak = get_weak();
        std::vector<UIElement> pending{ root };

        while (!pending.empty())
        {
            auto element = pending.back();
            pending.pop_back();

            if (auto panel = element.try_as<Controls::Panel>())
            {
                for (auto const& child : panel.Children())
                {
                    pending.push_back(child);
                }
            }
            else if (auto border = element.try_as<Controls::Border>(); border && border.Child())
            {
                pending.push_back(border.Child());
            }

            auto splitter = element.try_as<FrameworkElement>();
            std::wstring name = splitter ? std::wstring{ splitter.Name() } : L"";
            if (name.rfind(L"urusi-splitter:", 0) != 0)
            {
                continue;
            }

            bool horizontal = name.size() > 15 && name[15] == L'h';
            auto drag = std::make_shared<Drag>();

            // The cursor says it can be dragged, and which way. It is a
            // protected property, meant for a control to set of itself,
            // and this one is set from outside.
            splitter.as<IUIElementProtected>().ProtectedCursor(
                Microsoft::UI::Input::InputSystemCursor::Create(
                    horizontal ? Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast
                               : Microsoft::UI::Input::InputSystemCursorShape::SizeNorthSouth));

            // The grid the splitter is in, and which cell of it is the
            // splitter's; the parts are in the cells either side.
            auto cells = [splitter, horizontal]() {
                auto grid = splitter.Parent().try_as<Controls::Grid>();
                int index = horizontal ? Controls::Grid::GetColumn(splitter)
                                       : Controls::Grid::GetRow(splitter);
                return std::make_tuple(grid, index);
            };
            auto lengths = [horizontal](Controls::Grid const& grid, int index) {
                if (horizontal)
                {
                    return std::make_pair(grid.ColumnDefinitions().GetAt(index - 1).ActualWidth(),
                                          grid.ColumnDefinitions().GetAt(index + 1).ActualWidth());
                }
                return std::make_pair(grid.RowDefinitions().GetAt(index - 1).ActualHeight(),
                                      grid.RowDefinitions().GetAt(index + 1).ActualHeight());
            };

            splitter.PointerPressed([weak, splitter, horizontal, drag, cells, lengths](
                                        IInspectable const&, Input::PointerRoutedEventArgs const& args) {
                auto self = weak.get();
                auto [grid, index] = cells();
                if (!self || !grid || index < 1)
                {
                    return;
                }

                auto point = args.GetCurrentPoint(grid).Position();
                drag->active = true;
                drag->start = horizontal ? point.X : point.Y;
                std::tie(drag->before, drag->after) = lengths(grid, index);
                self->m_splitting = true;
                splitter.CapturePointer(args.Pointer());
                args.Handled(true);
            });

            splitter.PointerMoved([horizontal, drag, cells](
                                      IInspectable const&, Input::PointerRoutedEventArgs const& args) {
                auto [grid, index] = cells();
                if (!drag->active || !grid)
                {
                    return;
                }

                auto point = args.GetCurrentPoint(grid).Position();
                double moved = (horizontal ? point.X : point.Y) - drag->start;
                moved = std::clamp(moved, kMinimumPart - drag->before, drag->after - kMinimumPart);

                auto pixels = [](double value) { return GridLength{ value, GridUnitType::Pixel }; };
                auto shares = [](GridLength const& value) { return value.GridUnitType == GridUnitType::Star; };

                if (horizontal)
                {
                    auto before = grid.ColumnDefinitions().GetAt(index - 1);
                    auto after = grid.ColumnDefinitions().GetAt(index + 1);
                    if (shares(before.Width()) && !shares(after.Width()))
                    {
                        after.Width(pixels(drag->after - moved));
                    }
                    else
                    {
                        before.Width(pixels(drag->before + moved));
                    }
                }
                else
                {
                    auto before = grid.RowDefinitions().GetAt(index - 1);
                    auto after = grid.RowDefinitions().GetAt(index + 1);
                    if (shares(before.Height()) && !shares(after.Height()))
                    {
                        after.Height(pixels(drag->after - moved));
                    }
                    else
                    {
                        before.Height(pixels(drag->before + moved));
                    }
                }
                args.Handled(true);
            });

            auto finish = [weak, splitter, drag, cells, lengths, name](
                              IInspectable const&, Input::PointerRoutedEventArgs const& args) {
                auto self = weak.get();
                auto [grid, index] = cells();
                if (!self || !drag->active)
                {
                    return;
                }

                drag->active = false;
                self->m_splitting = false;
                splitter.ReleasePointerCapture(args.Pointer());

                JsonObject details;
                details.SetNamedValue(L"name", JsonValue::CreateStringValue(name));
                if (grid)
                {
                    auto [before, after] = lengths(grid, index);
                    details.SetNamedValue(L"before", JsonValue::CreateNumberValue(std::round(before)));
                    details.SetNamedValue(L"after", JsonValue::CreateNumberValue(std::round(after)));
                }
                self->SendHostEvent(L"splitter", details);

                // The frame was left the size it was while the splitter
                // moved, and is given the size it has now.
                self->SizeEmacsFrame();
                args.Handled(true);
            };
            splitter.PointerReleased(finish);
            splitter.PointerCaptureLost(finish);
        }
    }

    // Give the focus back to the element the keys go to Emacs from,
    // when nothing that is still on the screen has it. Anything Lisp
    // built may take the focus while it is there, a box to type in for
    // one, but the XAML around the rows is thrown away whenever it
    // changes, and a focus left on what was thrown away is a focus on
    // nothing: the keys would go nowhere until something was clicked.
    void MainWindow::KeepFocus()
    {
        auto root = Content().XamlRoot();
        if (!root)
        {
            return;
        }

        auto focused = Input::FocusManager::GetFocusedElement(root).try_as<UIElement>();
        if (!focused || !focused.XamlRoot())
        {
            InputSink().Focus(FocusState::Programmatic);
        }
    }

    // Write down, under urusi-debug-mode, what WHAT found: whether the
    // window is in front, and where the focus is, of Windows and of XAML.
    void MainWindow::TraceFocus(char const* what)
    {
        if (!m_debug)
        {
            return;
        }

        std::string xaml = "none";
        if (auto root = Content() ? Content().XamlRoot() : nullptr)
        {
            if (auto focused = Input::FocusManager::GetFocusedElement(root))
            {
                auto element = focused.try_as<FrameworkElement>();
                xaml = to_string(get_class_name(focused));
                if (element && !element.Name().empty())
                {
                    xaml += " " + to_string(element.Name());
                }
            }
        }

        char text[256];
        sprintf_s(text, "%s: foreground %s, active %s, win32 focus %p, xaml focus %s\n",
                  what, IsForeground() ? "yes" : "no", m_active ? "yes" : "no",
                  static_cast<void*>(GetFocus()), xaml.c_str());
        AppendLog("host", text);
    }

    // Whether this window is the one in front, as Windows has it: XAML
    // says the window has gone when it has not, now and then.
    bool MainWindow::IsForeground() const noexcept
    {
        return m_window && GetForegroundWindow() == m_window;
    }

    // Tell the input method again that the keys come here, when the
    // window comes back. The element kept the focus while the window was
    // away, so it is not given it again and says nothing, but the input
    // method went on to talk to whatever window was in front meanwhile.
    //
    // Not at once: when the window comes back, XAML puts its focus back
    // after telling it so, and until it has there is no focus to find.
    // Last in line, after whatever XAML does about coming back, and
    // only if the window is still in front by then.
    void MainWindow::ResumeComposition()
    {
        auto weak = get_weak();
        m_dispatcher.TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak] {
                auto self = weak.get();
                if (!self || !self->IsForeground())
                {
                    return;
                }

                auto root = self->Content().XamlRoot();
                if (!root)
                {
                    return;
                }

                self->KeepFocus();
                self->TraceFocus("resumed");
                if (Input::FocusManager::GetFocusedElement(root) == self->InputSink())
                {
                    self->m_composition.Focus(true);
                }
            });
    }

    // Tell Windows which parts of what Lisp drew are the title bar, when
    // the window has none of its own: the element named urusi-titlebar
    // moves the window and maximizes it when clicked twice, and the
    // controls on it are left to be clicked.
    void MainWindow::UpdateTitleBarRegions()
    {
        auto app = AppWindow();
        auto overlapped = app.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(app.Id());
        auto titlebar = Named(L"urusi-titlebar");

        if (!titlebar || !titlebar.XamlRoot() || (overlapped && overlapped.HasTitleBar()))
        {
            source.ClearRegionRects(Microsoft::UI::Input::NonClientRegionKind::Caption);
            source.ClearRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough);
            return;
        }

        // Windows counts in the pixels of the screen, from the corner of
        // the window's client area, which is where XAML's root is.
        double scale = titlebar.XamlRoot().RasterizationScale();
        auto rect = [scale](FrameworkElement const& element) {
            auto corner = element.TransformToVisual(nullptr).TransformPoint({ 0, 0 });
            return Windows::Graphics::RectInt32{
                static_cast<int32_t>(std::lround(corner.X * scale)),
                static_cast<int32_t>(std::lround(corner.Y * scale)),
                static_cast<int32_t>(std::lround(element.ActualWidth() * scale)),
                static_cast<int32_t>(std::lround(element.ActualHeight() * scale)) };
        };

        // Every control on the title bar, and not what is inside one: a
        // button is clicked as a whole.
        std::vector<Windows::Graphics::RectInt32> controls;
        std::function<void(DependencyObject const&)> collect = [&](DependencyObject const& parent) {
            int count = Media::VisualTreeHelper::GetChildrenCount(parent);
            for (int i = 0; i < count; ++i)
            {
                auto child = Media::VisualTreeHelper::GetChild(parent, i);
                auto control = child.try_as<Controls::Control>();
                if (control && control.IsHitTestVisible() && control.Visibility() == Visibility::Visible)
                {
                    controls.push_back(rect(control));
                }
                else
                {
                    collect(child);
                }
            }
        };
        collect(titlebar);

        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Caption, { rect(titlebar) });
        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough, controls);
    }

    void MainWindow::SizeEmacsFrame()
    {
        auto site = FrameSite();
        if (!site.XamlRoot() || m_splitting)
        {
            return;
        }

        // XAML works in device-independent pixels and Emacs in the ones
        // of the screen.
        double scale = site.XamlRoot().RasterizationScale();
        auto pixels = [scale](double value) { return static_cast<int>(std::lround(value * scale)); };

        SIZE size{ pixels(site.ActualWidth()), pixels(site.ActualHeight()) };

        for (auto const& panel : PanelSites())
        {
            SizeFrame(panel, std::wstring{ panel.Name() }.substr(12));
        }

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

    // The elements Lisp put the frames of panels in, named
    // urusi-frame:ID after the panel. They are frames of their own, each
    // as big as its element, as the frame the window shows is as big as
    // the element named urusi-frame.
    std::vector<FrameworkElement> MainWindow::PanelSites()
    {
        std::vector<FrameworkElement> sites;
        if (Surface().Children().Size() == 0)
        {
            return sites;
        }

        std::vector<UIElement> pending{ Surface().Children().GetAt(0) };
        while (!pending.empty())
        {
            auto element = pending.back();
            pending.pop_back();

            auto named = element.try_as<FrameworkElement>();
            if (named && std::wstring{ named.Name() }.rfind(L"urusi-frame:", 0) == 0)
            {
                sites.push_back(named);
            }
            if (auto panel = element.try_as<Controls::Panel>())
            {
                for (auto const& child : panel.Children())
                {
                    pending.push_back(child);
                }
            }
            else if (auto border = element.try_as<Controls::Border>(); border && border.Child())
            {
                pending.push_back(border.Child());
            }
        }
        return sites;
    }

    // Tell Emacs how big the frame of the panel ID is, from SITE, the
    // element it is shown in, when that has changed.
    void MainWindow::SizeFrame(FrameworkElement const& site, std::wstring const& id)
    {
        if (!site.XamlRoot() || m_splitting)
        {
            return;
        }

        double scale = site.XamlRoot().RasterizationScale();
        SIZE size{ static_cast<int>(std::lround(site.ActualWidth() * scale)),
                   static_cast<int>(std::lround(site.ActualHeight() * scale)) };
        auto& told = m_panelSizes[id];
        if (size.cx <= 0 || size.cy <= 0 || (size.cx == told.cx && size.cy == told.cy))
        {
            return;
        }
        told = size;

        JsonObject message;
        message.SetNamedValue(L"type", String(L"resize"));
        message.SetNamedValue(L"frame", String(hstring{ id }));
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

        // What this process was started with, before any of it is
        // changed. It depends on what started the application, and
        // Emacs hands it on to every program it runs.
        auto describe = [](DWORD which) {
            HANDLE handle = GetStdHandle(which);
            if (!handle || handle == INVALID_HANDLE_VALUE)
            {
                return std::string{ "none" };
            }

            DWORD flags = 0;
            GetHandleInformation(handle, &flags);
            char const* kinds[] = { "unknown", "disk", "char", "pipe" };
            DWORD kind = GetFileType(handle) & ~FILE_TYPE_REMOTE;
            return std::string{ kind < 4 ? kinds[kind] : "other" }
                + ((flags & HANDLE_FLAG_INHERIT) ? ", inheritable" : "");
        };
        AppendLog("host", "started with stdin " + describe(STD_INPUT_HANDLE)
                  + ", stdout " + describe(STD_OUTPUT_HANDLE)
                  + ", stderr " + describe(STD_ERROR_HANDLE) + "\n");

        // And whether whatever started it handed over a C runtime's
        // table of open files, which the runtime Emacs uses reads as
        // its own descriptors as it starts.
        STARTUPINFOW startup{ sizeof(startup) };
        GetStartupInfoW(&startup);
        AppendLog("host", "startup info flags 0x" + std::to_string(startup.dwFlags)
                  + ", runtime table " + std::to_string(startup.cbReserved2) + " bytes\n");

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
        m_lastHeard = std::chrono::steady_clock::now();

        // The first of each kind, so that a message that never comes is
        // as plain to see as one that fails.
        if (m_seen.insert(std::wstring{ type }).second)
        {
            AppendLog("host", "first " + to_string(type) + ", " + std::to_string(line.size())
                      + " bytes\n");
        }

        if (type == L"hello")
        {
            m_emacsReady = true;
            ShowStatus(L"");
            JsonObject reply;
            reply.SetNamedValue(L"type", String(L"hello"));
            reply.SetNamedValue(L"host", String(L"urusi-emacs"));
            reply.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
            reply.SetNamedValue(L"debug", JsonValue::CreateBooleanValue(m_debug));
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
        else if (type == L"debug")
        {
            m_debug = message.GetNamedBoolean(L"on", false);
            AppendLog("host", std::string{ "debug " } + (m_debug ? "on" : "off") + "\n");
        }
        else if (type == L"frame")
        {
            TakeEmacsWindow(reinterpret_cast<HWND>(
                static_cast<INT_PTR>(message.GetNamedNumber(L"window", 0))));
        }
        else if (type == L"call")
        {
            Call(message);
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

            // Where Lisp put the frame and the title bar goes with the
            // XAML around the rows, and they are new elements now.
            FollowLayout();
            KeepFocus();
        }

        auto root = Surface().Children().Size() ? Surface().Children().GetAt(0) : nullptr;
        auto named = root ? root.try_as<FrameworkElement>() : nullptr;

        for (auto const& value : message.GetNamedArray(L"rows", JsonArray{}))
        {
            auto group = value.GetObject();
            auto name = group.GetNamedString(L"panel", L"");
            auto target = named ? FindPanel(named, name) : nullptr;

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

    // The panel named NAME under ROOT. The XAML around the rows knows the
    // names in it, but a row is read on its own and keeps its names to
    // itself, so a panel inside a row is looked for among the rows.
    // Rows come before the rows inside them, so it is there by now.
    Controls::Panel MainWindow::FindPanel(FrameworkElement const& root, hstring const& name)
    {
        if (auto found = root.FindName(name))
        {
            return found.try_as<Controls::Panel>();
        }

        std::vector<UIElement> pending{ root };
        while (!pending.empty())
        {
            auto element = pending.back();
            pending.pop_back();

            auto panel = element.try_as<Controls::Panel>();
            if (panel && panel.Name() == name)
            {
                return panel;
            }
            if (panel)
            {
                for (auto const& child : panel.Children())
                {
                    pending.push_back(child);
                }
            }
            else if (auto border = element.try_as<Controls::Border>(); border && border.Child())
            {
                pending.push_back(border.Child());
            }
        }
        return nullptr;
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
                auto element = row.as<FrameworkElement>();
                element.Tag(box_value(key));
                // The row is read on its own, and its names are its own:
                // only the row can find what its events are on.
                AttachEvents(element, item.GetNamedArray(L"events", JsonArray{}));
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
            auto id = entry.GetNamedString(L"id", L"");

            // An element may be the row itself, which FindName does not
            // look at: it looks among what is inside.
            IInspectable target = root.Name() == name ? IInspectable{ root } : root.FindName(name);
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

    void MainWindow::SendEvent(hstring const& id, JsonObject const& args)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String(L"event"));
        message.SetNamedValue(L"id", String(id));
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

    // Do what Lisp asked the host to do, and answer it: now, or when the
    // person using the application has, for what waits on them.
    void MainWindow::Call(JsonObject const& message)
    {
        auto id = message.GetNamedNumber(L"id", 0);
        auto method = std::wstring{ message.GetNamedString(L"method", L"") };
        auto args = message.HasKey(L"args")
                && message.GetNamedValue(L"args").ValueType() == JsonValueType::Object
            ? message.GetNamedObject(L"args")
            : JsonObject{};
        auto weak = get_weak();

        urusi::HostCalls::Call(
            get_strong().as<Window>(), method, args,
            [weak, id](IJsonValue const& value, std::wstring const& error) {
                auto self = weak.get();
                if (!self)
                {
                    return;
                }

                JsonObject reply;
                reply.SetNamedValue(L"type", String(L"reply"));
                reply.SetNamedValue(L"id", JsonValue::CreateNumberValue(id));
                if (error.empty())
                {
                    reply.SetNamedValue(L"value", value);
                }
                else
                {
                    reply.SetNamedValue(L"error", String(hstring{ error }));
                }
                self->Send(reply);
            });
    }

    // Tell Lisp that something happened to the window that it did not
    // ask for, with what there is to know about it in DETAILS.
    void MainWindow::SendHostEvent(hstring const& name, JsonObject const& details)
    {
        details.SetNamedValue(L"type", String(L"host-event"));
        details.SetNamedValue(L"event", String(name));
        Send(details);
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
