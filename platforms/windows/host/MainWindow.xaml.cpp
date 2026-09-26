#include "pch.h"
#include "MainWindow.xaml.h"
#include "Window/DrawReader.h"
#include "Emacs/HostApi.h"
#include "Emacs/InputMessages.h"
#include "Input/KeyboardLayout.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

// For the window handle of this window, which is what tells Emacs that
// there is a host here at all.
#include <microsoft.ui.xaml.window.h>

// For what Windows has of an input method for the window, which the
// window itself never asks for and only writes down.
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <algorithm>
#include <string_view>
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


    // Empty a file to write, letting anything read it: nothing may look
    // at a log while it runs, which is the only time it is worth
    // looking at.
    HANDLE OpenToWrite(std::string const& path)
    {
        return CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    // A file beside the application, emptied when it starts, so that
    // what is in it is this run and not the last one.
    //
    // Under the number of the process if NAME cannot be had: the run
    // before keeps the file a moment after it has gone from the task
    // manager, and starting the application again then would otherwise
    // leave it with nothing written down at all.
    HANDLE OpenBesideTheApplication(char const* name)
    {
        char path[MAX_PATH]{};
        DWORD length = GetModuleFileNameA(nullptr, path, ARRAYSIZE(path));
        if (length == 0 || length == ARRAYSIZE(path))
        {
            return INVALID_HANDLE_VALUE;
        }

        std::string full{ path, length };
        auto slash = full.find_last_of('\\');
        if (slash == std::string::npos)
        {
            return INVALID_HANDLE_VALUE;
        }

        std::string directory = full.substr(0, slash + 1);
        HANDLE file = OpenToWrite(directory + name);
        if (file != INVALID_HANDLE_VALUE)
        {
            return file;
        }
        return OpenToWrite(directory + std::to_string(GetCurrentProcessId()) + "-" + name);
    }

    // What Windows has of an input method for the window the keys go to,
    // for the log: whether it has one at all, and whether it is turned
    // on. A key that turns it on and off does nothing when it has none,
    // and the keys arrive as the letters on them.
    std::string InputMethodState(HWND window)
    {
        if (!window)
        {
            return "no window has the keys";
        }

        HIMC context = ImmGetContext(window);
        if (!context)
        {
            return "no input method";
        }

        bool open = ImmGetOpenStatus(context) != FALSE;
        DWORD conversion = 0;
        DWORD sentence = 0;
        ImmGetConversionStatus(context, &conversion, &sentence);
        ImmReleaseContext(window, context);
        return std::string{ open ? "on" : "off" } + ", conversion " + std::to_string(conversion);
    }

    // The log, again, where it can be read without a debugger.
    void WriteToLogFile(std::string const& line)
    {
        static HANDLE file = OpenBesideTheApplication("urusi-emacs.log");

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        FlushFileBuffers(file);
    }

    // What the keyboard's side of the window saw, one line of JSON to a
    // thing, to be played back by the tests (tests/traces). Opened the
    // first time there is something to write, so that a run without
    // urusi-debug-mode leaves the last one's alone.
    void WriteToTraceFile(std::string const& line)
    {
        static HANDLE file = OpenBesideTheApplication("urusi-emacs.trace.jsonl");

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        FlushFileBuffers(file);
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
        SetThreadDescription(GetCurrentThread(), L"window");
        auto weak = get_weak();

        m_emacs = std::make_shared<urusi::windows::emacs::Emacs>(urusi::windows::emacs::Emacs::Events{
            .log = [this](char const* source, std::string const& text) { AppendLog(source, text); },
            .error = [this](hstring const& text) { ShowStatus(text); },
        });
        m_screen.emplace(Surface(), m_emacs);

        // Started under a debugger, as from Visual Studio, everything is
        // written down from the first key: whatever is being chased may
        // happen before there is a chance to ask for it.
        m_debug = IsDebuggerPresent() != FALSE;
        auto dispatcher = m_dispatcher;

        // Emacs posts from its own thread, and the window may only be
        // touched from this one.
        // A font file is taken here, on the thread that reads from
        // Emacs, rather than where the window is drawn: it may be tens
        // of megabytes, and reading it out of the message and making a
        // face of it would stop the window for as long.  Nothing of
        // that touches an element.
        auto fonts = m_fonts;
        fonts->OnWanting([emacs = m_emacs](int id) {
            JsonObject wanted;

            wanted.SetNamedValue(L"type", String(L"want-font"));
            wanted.SetNamedValue(L"id", JsonValue::CreateNumberValue(id));
            emacs->Send(wanted);
        });

        auto drawing = std::make_shared<urusi::core::window::DrawReader>();

        m_emacs->OnMessage([weak, dispatcher, fonts, drawing](std::string message) {
            constexpr std::string_view kFont{ "{\"type\":\"font\"" };
            constexpr std::string_view kDraw{ "{\"type\":\"draw\"" };

            if (message.compare(0, kFont.size(), kFont) == 0)
            {
                JsonObject said{ nullptr };

                if (JsonObject::TryParse(to_hstring(message), said))
                {
                    if (auto why = fonts->Take(said); !why.empty())
                    {
                        dispatcher.TryEnqueue([weak, why = std::move(why)] {
                            if (auto self = weak.get())
                            {
                                self->AppendLog("host", "font: " + why + "\n");
                            }
                        });
                    }
                }
                return;
            }

            // A screen comes as one line for each thing to draw, which
            // would be as many hops to the user interface thread; they
            // are gathered here and the screen goes over in one.
            if (message.compare(0, kDraw.size(), kDraw) == 0)
            {
                if (auto frame = drawing->Take(message))
                {
                    dispatcher.TryEnqueue([weak, said = std::move(*frame)] {
                        if (auto self = weak.get())
                        {
                            self->Drawn(said);
                        }
                    });
                }
                return;
            }

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
        urusi::windows::emacs::HostApi::Instance().SetWindow(window);

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
                self->m_keyboard.Activated();
            }
            else
            {
                self->m_keyboard.Deactivated();
            }
        });

        // Closing the window is Emacs's to decide, the way leaving Emacs
        // always has been, while Emacs can decide.
        AppWindow().Closing([weak](Microsoft::UI::Windowing::AppWindow const&,
                                   Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
            if (auto self = weak.get(); self && self->m_emacs->AskToClose())
            {
                args.Cancel(true);
            }
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
                self->m_emacs->SendHostEvent(L"state", details);
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
                    self->m_emacs->SendHostEvent(L"theme", details);
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

        // Once the window's content is in the tree, and not before: the
        // context is made for the view the content is shown in, and one
        // made while there is none is a context the input method never
        // sends anything to. Nothing says so; the keys simply arrive as
        // the letters on them and the key that turns it on does nothing.
        if (auto root = Content().try_as<FrameworkElement>(); root && !root.IsLoaded())
        {
            root.Loaded([weak](IInspectable const&, RoutedEventArgs const&) {
                if (auto self = weak.get())
                {
                    self->StartComposition();
                }
            });
        }
        else
        {
            StartComposition();
        }

        // Which build this is, so that a stale one is obvious.
        AppendLog("host", std::string{ "urusi-emacs built " } + __DATE__ + " " + __TIME__ + "\n");

        ShowStatus(L"Waiting for Emacs");
        ShowEventually();
    }

    void MainWindow::TakeEmacsWindow(HWND window)
    {
        m_emacsWindow = window;
        m_hasFrame = true;
        ShowStatus(L"");
        SizeEmacsFrame();
        TakeInputToEmacs();

        // The window may have come to the front before there was a
        // frame to tell.
        if (m_keyboard.Active())
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
    //
    // An Emacs that takes its input as messages is told in one.
    void MainWindow::TellEmacsFocus(bool focused)
    {
        if (m_emacs->InputAsMessages())
        {
            if (m_hasFrame)
            {
                m_emacs->Send(urusi::windows::emacs::FocusMessage(focused));
            }
        }
        else if (m_emacsWindow)
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
        RecordKeyboard();

        // The window the text services are to give the keys to is the
        // one Windows has, which is this window: what XAML puts inside
        // it is none of theirs.
        HWND window = nullptr;
        check_hresult(try_as<::IWindowNative>()->get_WindowHandle(&window));
        m_composition.Start(window, m_keyboard, [this](std::string const& line) {
            AppendLog("ime", line + "\n");
        });

        // The input method is the window's, not an element's: the keys
        // go to Emacs wherever in the window the focus is, but for a box
        // Lisp built to type in, which has an input method of its own.
        // So what the focus is on is looked at as it moves, and the
        // input method is left alone as it moves anywhere else, the log
        // under the screen that XAML moves it to when what had it goes.
        auto weak = get_weak();
        Input::FocusManager::GotFocus([weak](IInspectable const&,
                                             Input::FocusManagerGotFocusEventArgs const& args) {
            if (auto self = weak.get())
            {
                self->FocusMoved(args.NewFocusedElement());
            }
        });

        InputSink().Focus(FocusState::Programmatic);
    }

    // Tell the keyboard whether the keys come to Emacs, now that the
    // focus is on FOCUSED.
    void MainWindow::FocusMoved(IInspectable const& focused)
    {
        if (TakesText(focused))
        {
            m_keyboard.FocusLost();
        }
        else
        {
            m_keyboard.FocusGained();
        }
    }

    // Whether ELEMENT is one that takes the keys as text itself, with an
    // input method of its own.
    bool MainWindow::TakesText(IInspectable const& element)
    {
        return element && (element.try_as<Controls::TextBox>() || element.try_as<Controls::PasswordBox>()
                           || element.try_as<Controls::RichEditBox>());
    }

    // Everything that happens to the keyboard's side of the window,
    // written down while debugging, to be read, and to be played back
    // in a test: see tests/traces. Not debugging, the session is given
    // nothing to write to, and makes nothing to write.
    void MainWindow::RecordKeyboard()
    {
        if (!m_debug)
        {
            m_keyboard.Record({});
            return;
        }

        m_keyboard.Record([](std::string const& line) { WriteToTraceFile(line + "\n"); });
    }

    // Put TEXT into Emacs as the characters it is. Emacs reads them
    // the way it reads anything typed, so whatever is bound to them
    // runs.
    void MainWindow::TypeIntoEmacs(std::wstring const& text)
    {
        if (!m_hasFrame)
        {
            return;
        }

        // How much was typed and not what it was: what the input method
        // settled on belongs where a run is kept on purpose, which is
        // the trace urusi-debug-mode writes, and not in a log that every
        // run leaves behind.
        AppendLog("host", "type " + std::to_string(text.size()) + " characters\n");

        // All at once, not a character at a time: each character sent
        // as a key is a command of its own, drawn after, and the text
        // appears as though it were being typed again. Lisp puts the
        // whole of it before any other input, or, for an Emacs that
        // takes its input as messages, its C does, in the order of the
        // keys around it.
        JsonObject message;
        message.SetNamedValue(L"type", String(m_emacs->InputAsMessages() ? L"text" : L"commit"));
        message.SetNamedValue(L"text", String(hstring{ text }));
        m_emacs->Send(message);
    }

    // Where Emacs says the caret is, in the pixels of the screen, so
    // that the candidates appear beside it.
    // Show the pointer the shape Emacs asked for.
    //
    // Emacs has no pointer of its own here: it works out what is under
    // this one as it moves and says what it should look like, and the
    // window is the one that has it.
    void MainWindow::ShowPointer(hstring const& shape)
    {
        using Microsoft::UI::Input::InputCursor;
        using Microsoft::UI::Input::InputSystemCursor;
        using Microsoft::UI::Input::InputSystemCursorShape;

        if (shape == m_pointerShape)
        {
            return;
        }
        m_pointerShape = shape;

        InputSystemCursorShape wanted = InputSystemCursorShape::Arrow;
        if (shape == L"text") { wanted = InputSystemCursorShape::IBeam; }
        else if (shape == L"hand") { wanted = InputSystemCursorShape::Hand; }
        else if (shape == L"busy") { wanted = InputSystemCursorShape::Wait; }
        else if (shape == L"horizontal-drag") { wanted = InputSystemCursorShape::SizeWestEast; }
        else if (shape == L"vertical-drag") { wanted = InputSystemCursorShape::SizeNorthSouth; }
        else if (shape == L"left-edge" || shape == L"right-edge")
        {
            wanted = InputSystemCursorShape::SizeWestEast;
        }
        else if (shape == L"top-edge" || shape == L"bottom-edge")
        {
            wanted = InputSystemCursorShape::SizeNorthSouth;
        }
        else if (shape == L"top-left-corner" || shape == L"bottom-right-corner")
        {
            wanted = InputSystemCursorShape::SizeNorthwestSoutheast;
        }
        else if (shape == L"top-right-corner" || shape == L"bottom-left-corner")
        {
            wanted = InputSystemCursorShape::SizeNortheastSouthwest;
        }

        // Set on the element the pointer is actually over, which is
        // where the screen is drawn: EditorSite is behind it, the
        // pointer never reaches it, and a shape set there is never
        // shown. Children take it from here unless they set their own.
        //
        // The property is the element's own to set, and is reached
        // through the interface that carries what an element keeps to
        // itself.
        if (auto site = Surface().try_as<IUIElementProtected>())
        {
            site.ProtectedCursor(InputSystemCursor::Create(wanted));
        }
    }

    void MainWindow::Caret(JsonObject const& message)
    {
        HWND window = nullptr;
        auto site = FrameSite();

        if (!site.XamlRoot()
            || FAILED(try_as<::IWindowNative>()->get_WindowHandle(&window)))
        {
            return;
        }

        // Emacs counts from the corner of the area it was given, in the
        // pixels of the screen; the text services want the corner of the
        // screen, in those same pixels. Only what XAML laid out is in
        // the 96ths of an inch it counts in, and is scaled up to meet
        // them.
        double scale = site.XamlRoot().RasterizationScale();
        auto corner = site.TransformToVisual(Content()).TransformPoint({ 0, 0 });
        POINT client{ 0, 0 };

        ClientToScreen(window, &client);

        LONG left = client.x + std::lround(corner.X * scale)
                    + std::lround(message.GetNamedNumber(L"x", 0));
        LONG top = client.y + std::lround(corner.Y * scale)
                   + std::lround(message.GetNamedNumber(L"y", 0));

        // The width Emacs gives the cursor is the width of what it sits
        // on, which is how wide a character of the composition is drawn.
        LONG advance = std::lround(message.GetNamedNumber(L"width", 2));

        m_composition.SetCaret({ left, top, left + advance,
                                 top + std::lround(message.GetNamedNumber(L"height", 16)) },
                               advance);
    }

    // Give ARGS to the Emacs frame as the key message it was, and let
    // Emacs make of it what it makes of any other. Only the key goes:
    // Emacs turns it into a character itself, from the state of the
    // keyboard, which is this thread's as well now that the two input
    // queues are one.
    //
    // An Emacs that takes its input as messages has no layout of its own
    // to read the key with, and is sent what the layout makes of it.
    void MainWindow::ForwardKey(Input::KeyRoutedEventArgs const& args, bool down)
    {
        if (!m_hasFrame)
        {
            return;
        }

        auto status = args.KeyStatus();
        urusi::core::input::KeyEvent key{
            .key = static_cast<int>(args.Key()),
            .repeat = status.RepeatCount,
            .scanCode = status.ScanCode,
            .extended = status.IsExtendedKey,
            .menuDown = status.IsMenuKeyDown,
            .wasDown = status.WasKeyDown,
            .down = down,
        };
        m_keyboard.Key(key);

        // What the input method settled on goes to Emacs before the key
        // that settled it: it reads the composition when its turn comes
        // round, and the key is Emacs's at once.
        m_composition.FlushComposition();

        // Who the key belongs to, in order: the input method, then
        // Windows, then Emacs.
        //
        // The input method looks first, as it would have if this were a
        // window of Windows's own: the window reads its keys where what
        // draws it hands them over, which is past the point where they
        // would have been offered.
        if (m_composition.TakesKey(key))
        {
            args.Handled(true);
            return;
        }

        if (down && urusi::windows::input::IsInputMethodSwitch(key.key))
        {
            m_composition.EndComposition();
            AppendLog("host", "input method switched: " + InputMethodState(GetFocus()) + "\n");
        }

        // Then Windows: turning the input method on and off is its
        // arrangement with the keyboard, and no part of it is Emacs's,
        // whether or not the input method took the key.
        if (urusi::windows::input::IsInputMethodChord(
                key, ImmIsIME(GetKeyboardLayout(0)) != FALSE))
        {
            args.Handled(true);
            return;
        }

        if (m_emacs->InputAsMessages())
        {
            if (auto typed = urusi::windows::input::ReadKey(key))
            {
                m_emacs->Send(urusi::windows::emacs::KeyMessage(*typed));
                args.Handled(true);
            }
            return;
        }

        auto message = urusi::windows::input::TranslateKey(key);
        if (!message)
        {
            return;
        }

        if (down && m_debug)
        {
            AppendLog("host", "key " + std::to_string(static_cast<int>(args.Key())) + "\n");
        }
        PostMessageW(m_emacsWindow, message->message, message->wParam, message->lParam);
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
        return m_screen ? m_screen->Named(name) : nullptr;
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

        // A view of each frame shown: the one the window shows, in the
        // element named urusi-frame, and those of panels, in elements
        // named urusi-frame:ID, whose Tag is the number of the frame's
        // window. An Emacs that takes its input as messages is sent the
        // pointer instead, as it has no windows.
        m_frameViews.clear();
        urusi::windows::window::XamlFrameView::Frame root{
            .window = [weak]() -> HWND {
                auto self = weak.get();
                return self ? self->m_emacsWindow : nullptr;
            },
            .tellSize = [weak](std::wstring const& id, urusi::core::window::PixelSize size) {
                if (auto self = weak.get())
                {
                    self->TellFrameSize(id, size);
                }
            },
        };
        if (m_emacs->InputAsMessages())
        {
            root.sendPointer = [weak](std::wstring const& id,
                                      urusi::core::input::PointerEvent const& event, double scale) {
                auto self = weak.get();
                auto pointer = urusi::windows::input::PointerForEmacs(event, scale);
                if (self && pointer)
                {
                    self->m_emacs->Send(urusi::windows::emacs::PointerMessage(id, *pointer));
                }
            };
        }

        if (auto site = Named(L"urusi-frame"))
        {
            site.SizeChanged(changed);
            m_frameViews.push_back(urusi::windows::window::XamlFrameView::Attach(site, L"", root, m_frameSizes));
        }

        // The chrome is built again whenever it changes, which leaves
        // every picture laid over where its frame used to be.
        for (auto& [name, picture] : m_pictures)
        {
            ShowPictureIn(name);
        }

        for (auto& [name, drawing] : m_drawings)
        {
            if (auto site = Walked(L"urusi-emacs:" + name))
            {
                drawing.Attach(site);
            }
        }

        for (auto const& panel : PanelSites())
        {
            HWND window = nullptr;
            if (auto tag = panel.Tag().try_as<hstring>())
            {
                window = reinterpret_cast<HWND>(
                    static_cast<INT_PTR>(std::wcstoll(tag->c_str(), nullptr, 10)));
            }

            auto frame = root;
            frame.window = [window] { return window; };
            panel.SizeChanged(changed);
            m_frameViews.push_back(urusi::windows::window::XamlFrameView::Attach(
                panel, std::wstring{ panel.Name() }.substr(12), frame, m_frameSizes));
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

    // Let the splitters Lisp put between the parts of a layout be
    // dragged: each element named as one becomes a urusi::core::window::Splitter.
    // Where one is let go is Lisp's to remember, and it is told.
    void MainWindow::AttachSplitters(UIElement const& root)
    {
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

            auto named = element.try_as<FrameworkElement>();
            if (!named)
            {
                continue;
            }

            hstring name = named.Name();
            urusi::windows::window::XamlSplitter::Attach(named, {
                .started = [weak] {
                    if (auto self = weak.get())
                    {
                        self->m_splitting = true;
                    }
                },
                .finished = [weak, name](double before, double after) {
                    auto self = weak.get();
                    if (!self)
                    {
                        return;
                    }

                    self->m_splitting = false;
                    JsonObject details;
                    details.SetNamedValue(L"name", JsonValue::CreateStringValue(name));
                    details.SetNamedValue(L"before", JsonValue::CreateNumberValue(std::round(before)));
                    details.SetNamedValue(L"after", JsonValue::CreateNumberValue(std::round(after)));
                    self->m_emacs->SendHostEvent(L"splitter", details);

                    // The frame was left the size it was while the
                    // splitter moved, and is given the size it has now.
                    self->SizeEmacsFrame();
                },
            });
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
                  what, IsForeground() ? "yes" : "no", m_keyboard.Active() ? "yes" : "no",
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
                if (!self)
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
                bool keysComeHere = !TakesText(Input::FocusManager::GetFocusedElement(root));
                self->m_keyboard.ResumeChecked(self->IsForeground(), keysComeHere);
            });
    }

    // Look again, once what XAML is doing about it has been done,
    // whether the window really has gone.
    void MainWindow::CheckDeactivation()
    {
        auto weak = get_weak();
        m_dispatcher.TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak] {
                if (auto self = weak.get())
                {
                    self->TraceFocus("deactivated, checked");
                    self->m_keyboard.DeactivationChecked(self->IsForeground());
                }
            });
    }

    // What the Keyboard tells this window.
    void MainWindow::Effects::CheckLater() { window->CheckDeactivation(); }
    void MainWindow::Effects::ResumeLater() { window->ResumeComposition(); }
    void MainWindow::Effects::Commit(std::wstring const& text) { window->TypeIntoEmacs(text); }

    void MainWindow::Effects::TellEmacsFocus(bool focused)
    {
        window->TellEmacsFocus(focused);
        window->m_emacs->SendHostEvent(focused ? L"activated" : L"deactivated", JsonObject{});
    }

    void MainWindow::Effects::Composing(urusi::core::input::Composition const& composition)
    {
        using urusi::core::input::Underline;
        static constexpr wchar_t const* lines[] = {
            L"none", L"solid", L"dotted", L"dashed", L"wavy", L"double",
        };

        JsonObject message;
        JsonArray runs;

        for (auto const& run : composition.runs)
        {
            JsonObject one;

            one.SetNamedValue(L"length", JsonValue::CreateNumberValue(
                static_cast<double>(run.length)));
            one.SetNamedValue(L"underline",
                              String(lines[static_cast<size_t>(run.underline)]));
            if (!run.foreground.empty())
            {
                one.SetNamedValue(L"foreground", String(to_hstring(run.foreground)));
                one.SetNamedValue(L"background", String(to_hstring(run.background)));
            }
            runs.Append(one);
        }

        message.SetNamedValue(L"type", String(L"composition"));
        message.SetNamedValue(L"text", String(hstring{ composition.text }));
        message.SetNamedValue(L"caret", JsonValue::CreateNumberValue(
            static_cast<double>(composition.caret)));
        message.SetNamedValue(L"runs", runs);
        window->m_emacs->Send(message);
    }

    // Tell Windows which parts of what Lisp drew are the title bar, when
    // the window has none of its own: the element named urusi-titlebar
    // moves the window and maximizes it when clicked twice, and the
    // controls on it are left to be clicked.
    void MainWindow::UpdateTitleBarRegions()
    {
        if (!m_captionRegions)
        {
            m_captionRegions.emplace(AppWindow());
        }
        m_captionRegions->Update(Named(L"urusi-titlebar"));
    }

    void MainWindow::SizeEmacsFrame()
    {
        if (m_splitting)
        {
            return;
        }

        bool shown = false;
        for (auto const& view : m_frameViews)
        {
            view->Resize();
            shown = shown || view->Id().empty();
        }
        if (shown)
        {
            return;
        }

        // Lisp put the frame the window shows in no element of its own:
        // it is as big as the window.
        auto site = FrameSite();
        if (!site.XamlRoot())
        {
            return;
        }
        if (auto size = m_frameSizes.Resized(L"", site.ActualWidth(), site.ActualHeight(),
                                             site.XamlRoot().RasterizationScale()))
        {
            TellFrameSize(L"", *size);
        }
    }

    // The elements Lisp put the frames of panels in, named
    // urusi-frame:ID after the panel. They are frames of their own, each
    // as big as its element, as the frame the window shows is as big as
    // the element named urusi-frame.
    // The element of the screen named NAME, walked for rather than
    // asked for: every row of the screen is parsed on its own, so the
    // names in it are that row's and FindName never sees them.
    FrameworkElement MainWindow::Walked(std::wstring const& name)
    {
        if (Surface().Children().Size() == 0)
        {
            return nullptr;
        }

        std::vector<UIElement> pending{ Surface().Children().GetAt(0) };
        while (!pending.empty())
        {
            auto element = pending.back();
            pending.pop_back();

            if (auto named = element.try_as<FrameworkElement>();
                named && named.Name() == name)
            {
                return named;
            }
            if (auto panel = element.try_as<Controls::Panel>())
            {
                for (auto const& child : panel.Children())
                {
                    pending.push_back(child);
                }
            }
            else if (auto border = element.try_as<Controls::Border>();
                     border && border.Child())
            {
                pending.push_back(border.Child());
            }
        }
        return nullptr;
    }

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

    // Tell Emacs how big the frame ID is: the panel's, or the empty one
    // for the frame the window shows.
    void MainWindow::TellFrameSize(std::wstring const& id, urusi::core::window::PixelSize size)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String(L"resize"));
        if (!id.empty())
        {
            message.SetNamedValue(L"frame", String(hstring{ id }));
        }
        message.SetNamedValue(L"width", JsonValue::CreateNumberValue(size.width));
        message.SetNamedValue(L"height", JsonValue::CreateNumberValue(size.height));
        m_emacs->Send(message);
    }

    void MainWindow::StartEmacs()
    {
        auto weak = get_weak();
        auto dispatcher = m_dispatcher;

        m_emacs->Start(
            [weak, dispatcher](char const* source, std::string text) {
                dispatcher.TryEnqueue([weak, source, text = std::move(text)] {
                    if (auto self = weak.get())
                    {
                        self->AppendLog(source, text);
                    }
                });
            },
            [weak, dispatcher] {
                dispatcher.TryEnqueue([weak] {
                    if (auto self = weak.get())
                    {
                        self->EmacsExited();
                    }
                });
            });
    }

    // An Emacs of its own process has gone. Leaving Emacs leaves the
    // application, as it does when Emacs is in this process and ends
    // it; but an Emacs that went before it said anything leaves the
    // window, and the log in it, to say what went wrong.
    void MainWindow::EmacsExited()
    {
        bool running = m_emacs->Ready();
        m_emacs->Exited();

        // Before anything else, and whether or not the window is to go:
        // a picture is a bitmap and stays as it was drawn, so a frame
        // left on the screen says that Emacs is there when it is not,
        // and says it for as long as the window takes to close.
        for (auto& [name, picture] : m_pictures)
        {
            picture.TakeAway();
        }
        m_pictures.clear();

        if (running)
        {
            Close();
            return;
        }

        ShowStatus(L"Emacs exited");
        ShowWhenReady();
    }

    void MainWindow::OnMessage(std::string const& line)
    {
        auto message = m_emacs->Receive(line);
        if (!message)
        {
            return;
        }

        auto type = message.GetNamedString(L"type", L"");
        if (type == L"hello")
        {
            ShowStatus(L"");
            m_emacs->Hello(m_debug, Content() && Content().XamlRoot()
                                        ? Content().XamlRoot().RasterizationScale()
                                        : 1.0);

            // An Emacs of its own process says nothing of its frame,
            // which has no window: it made it before it said hello.
            if (m_emacs->InputAsMessages())
            {
                TakeEmacsWindow(nullptr);
            }
        }
        else if (type == L"screen")
        {
            Screen(message);
        }
        else if (type == L"pointer")
        {
            ShowPointer(message.GetNamedString(L"shape"));
        }
        else if (type == L"picture")
        {
            Picture(message);
        }
        else if (type == L"caret")
        {
            Caret(message);
        }
        else if (type == L"measure")
        {
            m_screen->Measure(message);
        }
        else if (type == L"log")
        {
            AppendLog("emacs", to_string(message.GetNamedString(L"text", L"")) + "\n");
        }
        else if (type == L"debug")
        {
            m_debug = message.GetNamedBoolean(L"on", false);
            RecordKeyboard();
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
            m_emacs->SendError(L"unknown message type: " + type);
        }
    }

    // Lay the picture of the frame NAME over the element Lisp named
    // for it, and say whether there was one to lay it over.
    bool MainWindow::ShowPictureIn(std::wstring const& name)
    {
        auto shown = Walked(L"urusi-emacs:" + name);
        auto found = m_pictures.find(name);

        if (!shown || found == m_pictures.end())
        {
            return false;
        }

        auto& picture = found->second;
        auto weak = get_weak();

        picture.Attach(shown);
        // Said only when the element moved or changed size, never after
        // every layout: the picture is laid out to follow it, and that
        // would call for another layout each time.
        shown.SizeChanged([weak, shown, name](IInspectable const&,
                                              SizeChangedEventArgs const&) {
            if (auto self = weak.get())
            {
                if (auto it = self->m_pictures.find(name); it != self->m_pictures.end())
                {
                    it->second.Follow(shown);
                }
            }
        });
        return true;
    }

    // A screen Emacs said rather than drew, which the host draws.
    void MainWindow::Drawn(urusi::core::window::DrawFrame const& said)
    {
        auto found = m_drawings.find(said.frame);

        if (found == m_drawings.end())
        {
            found = m_drawings
                        .emplace(said.frame,
                                 urusi::windows::window::XamlDrawing{ m_fonts })
                        .first;
        }

        // A picture of the same frame from before Emacs was told to
        // say what it draws is still laid over the element, and would
        // be laid over this: it is a bitmap and never changes again.
        if (auto stale = m_pictures.find(said.frame); stale != m_pictures.end())
        {
            stale->second.TakeAway();
            m_pictures.erase(stale);
        }

        // The element may not be there yet: Emacs draws a frame before
        // the screen saying where it goes has been built.
        if (auto site = Walked(L"urusi-emacs:" + said.frame))
        {
            found->second.Attach(site);
        }

        if (auto why = found->second.Draw(said); !why.empty())
        {
            AppendLog("host", "draw: " + why + "\n");
        }
    }

    void MainWindow::Picture(JsonObject const& message)
    {
        std::wstring name{ message.GetNamedString(L"frame", L"") };
        auto& picture = m_pictures[name];

        // The element may not be there yet: Emacs draws a frame before
        // the screen saying where it is has been built.  The picture is
        // kept all the same, and laid over the element once there is
        // one, with everything drawn into it so far.
        ShowPictureIn(name);

        // A picture that cannot be shown is said rather than passed
        // over quietly: Emacs goes on drawing into the same picture, so
        // the next one will not put right what this one would have.
        auto began = std::chrono::steady_clock::now();
        auto why = picture.Show(message);
        if (!why.empty())
        {
            AppendLog("host", "picture: " + why + "\n");
        }

        // The first of them say what showing one costs, as the screens
        // do: long enough to see what a keystroke takes, and then quiet.
        static int told;
        if (told < 30)
        {
            auto took = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - began)
                            .count();
            char said[96];

            snprintf(said, sizeof said, "picture %d shown in %.1fms\n", ++told, took);
            AppendLog("host", said);
        }
    }

    void MainWindow::Screen(JsonObject const& message)
    {
        ShowWhenReady();

        if (m_screen->ShowChrome(message))
        {
            ShowStatus(L"");

            // Where Lisp put the frame and the title bar goes with the
            // XAML around the rows, and they are new elements now.
            FollowLayout();
            KeepFocus();
        }
        m_screen->ShowRows(message);
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
        std::weak_ptr<urusi::windows::emacs::Emacs> emacs = m_emacs;

        urusi::windows::emacs::HostCalls::Call(
            get_strong().as<Window>(), method, args,
            [emacs, id](IJsonValue const& value, std::wstring const& error) {
                if (auto self = emacs.lock())
                {
                    self->Reply(id, value, error);
                }
            });
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
