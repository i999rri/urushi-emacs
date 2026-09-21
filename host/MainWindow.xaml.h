#pragma once

#include "MainWindow.g.h"
#include "Composition.h"
#include "EmacsHost.h"
#include "HostApi.h"
#include "HostCalls.h"
#include "KeyTranslation.h"
#include "MouseTranslation.h"
#include "FrameSizes.h"
#include "Rows.h"
#include "XamlFrameView.h"
#include "XamlSplitter.h"

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace winrt::urusi_emacs::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        // Takes the messages of Emacs once the named elements (Surface,
        // Status, Log) exist, which is only after the generated
        // InitializeComponent.
        void InitializeComponent();

    private:
        void Start();
        void StartEmacs();

        // The Emacs frame is a window of Emacs's own, on no screen at
        // all: it is there to be posted to. Emacs says when it exists;
        // how big the frame is is this window's business from then on.
        void TakeEmacsWindow(HWND window);
        void TakeInputToEmacs();
        void TellEmacsFocus(bool focused);
        void StartComposition();
        void RecordKeyboard();
        void TypeIntoEmacs(std::wstring const& text);
        void Caret(Windows::Data::Json::JsonObject const& message);
        void ForwardKey(Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args,
                        bool down);
        void SizeEmacsFrame();

        // Runs on the UI thread.
        void OnMessage(std::string const& line);
        void Screen(Windows::Data::Json::JsonObject const& message);
        void Measure(Windows::Data::Json::JsonObject const& message);
        Microsoft::UI::Xaml::Controls::Panel FindPanel(Microsoft::UI::Xaml::FrameworkElement const& root,
                                                       winrt::hstring const& name);
        void ReconcileRows(Microsoft::UI::Xaml::Controls::Panel const& panel,
                           Windows::Data::Json::JsonArray const& items);
        void AttachEvents(Microsoft::UI::Xaml::FrameworkElement const& root,
                          Windows::Data::Json::JsonArray const& events);

        void Call(Windows::Data::Json::JsonObject const& message);

        // What Lisp built, found by the names it gave.
        Microsoft::UI::Xaml::FrameworkElement Named(winrt::hstring const& name);
        Microsoft::UI::Xaml::FrameworkElement FrameSite();
        void FollowLayout();
        void UpdateTitleBarRegions();
        void AttachSplitters(Microsoft::UI::Xaml::UIElement const& root);
        std::vector<Microsoft::UI::Xaml::FrameworkElement> PanelSites();
        void TellFrameSize(std::wstring const& id, urusi::PixelSize size);
        void KeepFocus();
        void ResumeComposition();
        void CheckDeactivation();
        bool IsForeground() const noexcept;
        void TraceFocus(char const* what);
        void SendHostEvent(winrt::hstring const& name,
                           Windows::Data::Json::JsonObject const& details);

        // Safe from any thread.
        void SendEvent(winrt::hstring const& id, Windows::Data::Json::JsonObject const& args);
        void SendError(winrt::hstring const& message);
        void Send(Windows::Data::Json::JsonObject const& message);

        void ShowStatus(winrt::hstring const& text);
        // The window is not shown until Emacs has a screen to put in
        // it, or until long enough has passed that whatever is in it
        // is worth seeing anyway.
        void ShowWhenReady();
        void ShowEventually();
        // SOURCE says which side a line came from, since both
        // write here and they fail in different ways.
        void AppendLog(char const* source, std::string const& text);

        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::string m_log;
        std::set<std::wstring> m_seen;
        HWND m_emacsWindow{ nullptr };

        // How big each frame was last told it is, so that it is only
        // told when that changes.
        urusi::FrameSizes m_frameSizes;

        // A view of each frame on the screen, made again with what Lisp
        // built around them.
        std::vector<std::shared_ptr<urusi::XamlFrameView>> m_frameViews;
        bool m_attached{ false };

        // The window itself, to ask Windows whether it is in front.
        HWND m_window{ nullptr };

        // Whether each key and what the input method says are written
        // down: from the start under a debugger, or once urusi-debug-mode
        // asks. A day of it is a large file.
        bool m_debug{ false };

        // Whether a splitter is being dragged. The frame is not resized
        // while it is: resizing it has Emacs lay out and draw again, and
        // what it draws replaces the grid the splitter is in.
        bool m_splitting{ false };
        bool m_shown{ false };

        // Whether Emacs can be asked yet, when it was last heard from,
        // and when it was last asked to close: closing the window is
        // Emacs's to decide only while it can decide.
        bool m_emacsReady{ false };
        std::chrono::steady_clock::time_point m_lastHeard{};
        std::chrono::steady_clock::time_point m_closeAsked{};

        // How the window takes up the screen, as Lisp was last told.
        std::wstring m_windowState{ L"normal" };
        urusi::Composition m_composition;

        // What the Keyboard tells this window.
        struct Effects : urusi::KeyboardEvents
        {
            explicit Effects(MainWindow* owner) : window(owner) {}

            void CheckLater() override;
            void ResumeLater() override;
            void TellEmacsFocus(bool focused) override;
            void Commit(std::wstring const& text) override;
            void Composing(std::wstring const& text) override;

            MainWindow* window;
        };

        // The keyboard's side of the window: which window has it, what
        // becomes of each key, and what the input method makes of them.
        Effects m_effects{ this };
        urusi::Keyboard m_keyboard{ m_composition, m_effects };
    };
}

namespace winrt::urusi_emacs::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
