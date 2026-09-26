#pragma once

#include "MainWindow.g.h"
#include "Input/TextServices.h"
#include "Emacs/Emacs.h"
#include "Emacs/HostCalls.h"
#include "Input/KeyTranslation.h"
#include "Input/MouseTranslation.h"
#include "Window/FrameSizes.h"
#include "Window/Rows.h"
#include "Window/XamlCaptionRegions.h"
#include "Window/XamlFrameView.h"
#include "Window/XamlFonts.h"
#include "Window/XamlGlyphs.h"
#include "Window/XamlPicture.h"
#include "Window/XamlScreen.h"
#include "Window/XamlSplitter.h"

#include <chrono>
#include <map>
#include <optional>
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
        // An Emacs of its own process has no window, and its frame is
        // there once it has said hello.
        void TakeEmacsWindow(HWND window);
        void EmacsExited();
        void TakeInputToEmacs();
        void TellEmacsFocus(bool focused);
        void StartComposition();
        void RecordKeyboard();
        void TypeIntoEmacs(std::wstring const& text);
        void Caret(Windows::Data::Json::JsonObject const& message);
        void ShowPointer(winrt::hstring const& shape);
        void ForwardKey(Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args,
                        bool down);
        void SizeEmacsFrame();

        // Runs on the UI thread.
        void OnMessage(std::string const& line);
        void Screen(Windows::Data::Json::JsonObject const& message);
        void Picture(Windows::Data::Json::JsonObject const& message);
        void Drawn(urusi::core::window::DrawFrame const& said);
        bool ShowPictureIn(std::wstring const& name);
        Microsoft::UI::Xaml::FrameworkElement Walked(std::wstring const& name);

        void Call(Windows::Data::Json::JsonObject const& message);

        // What Lisp built, found by the names it gave.
        Microsoft::UI::Xaml::FrameworkElement Named(winrt::hstring const& name);
        Microsoft::UI::Xaml::FrameworkElement FrameSite();
        void FollowLayout();
        void UpdateTitleBarRegions();
        void AttachSplitters(Microsoft::UI::Xaml::UIElement const& root);
        std::vector<Microsoft::UI::Xaml::FrameworkElement> PanelSites();
        void TellFrameSize(std::wstring const& id, urusi::core::window::PixelSize size);
        void KeepFocus();
        void FocusMoved(winrt::Windows::Foundation::IInspectable const& focused);
        static bool TakesText(winrt::Windows::Foundation::IInspectable const& element);
        void ResumeComposition();
        void CheckDeactivation();
        bool IsForeground() const noexcept;
        void TraceFocus(char const* what);

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

        // Emacs, and the screen it builds in the window. Emacs is shared
        // with what the screen's elements send it, which can outlive
        // the window.
        std::shared_ptr<urusi::windows::emacs::Emacs> m_emacs;
        std::optional<urusi::windows::window::XamlScreen> m_screen;
        // The screen Emacs drew for each frame, by the name it is
        // known by, since a child frame has a picture of its own.
        std::map<std::wstring, urusi::windows::window::XamlPicture> m_pictures;
        std::shared_ptr<urusi::windows::window::XamlGlyphs> m_glyphs;
        // The font files Emacs draws in, which it hands over because a
        // glyph is numbered by the file it is in.  Shared, since they
        // are taken on the thread that reads from Emacs and outlive
        // nothing else here.
        std::shared_ptr<urusi::windows::window::XamlFonts> m_fonts
            = std::make_shared<urusi::windows::window::XamlFonts>();
        std::string m_log;
        std::set<std::wstring> m_seen;
        HWND m_emacsWindow{ nullptr };
        bool m_hasFrame{ false };

        // How big each frame was last told it is, so that it is only
        // told when that changes.
        urusi::core::window::FrameSizes m_frameSizes;

        // A view of each frame on the screen, made again with what Lisp
        // built around them.
        std::vector<std::shared_ptr<urusi::windows::window::XamlFrameView>> m_frameViews;

        // The part of the window that moves it, where Lisp drew a title
        // bar: made once the window has an AppWindow to tell.
        std::optional<urusi::windows::window::XamlCaptionRegions> m_captionRegions;
        bool m_attached{ false };

        // The window itself, to ask Windows whether it is in front.
        HWND m_window{ nullptr };

        // Whether each key and what the input method says are written
        // down: from the start under a debugger, or once urusi-debug-mode
        // asks. A day of it is a large file.
        bool m_debug{ false };

        // The shape Emacs last asked the pointer to take.
        winrt::hstring m_pointerShape;

        // Whether a splitter is being dragged. The frame is not resized
        // while it is: resizing it has Emacs lay out and draw again, and
        // what it draws replaces the grid the splitter is in.
        bool m_splitting{ false };
        bool m_shown{ false };

        // How the window takes up the screen, as Lisp was last told.
        std::wstring m_windowState{ L"normal" };
        urusi::windows::input::TextServices m_composition;

        // What the Keyboard tells this window.
        struct Effects : urusi::core::input::KeyboardEvents
        {
            explicit Effects(MainWindow* owner) : window(owner) {}

            void CheckLater() override;
            void ResumeLater() override;
            void TellEmacsFocus(bool focused) override;
            void Commit(std::wstring const& text) override;
            void Composing(urusi::core::input::Composition const& composition) override;

            MainWindow* window;
        };

        // The keyboard's side of the window: which window has it, what
        // becomes of each key, and what the input method makes of them.
        Effects m_effects{ this };
        urusi::core::input::Keyboard m_keyboard{ m_composition, m_effects };
    };
}

namespace winrt::urusi_emacs::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
