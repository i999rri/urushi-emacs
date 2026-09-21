#pragma once

#include "MainWindow.g.h"
#include "Composition.h"
#include "EmacsHost.h"
#include "HostApi.h"
#include "HostCalls.h"

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
        void StartComposition();
        void TypeIntoEmacs(std::wstring const& text);
        void Caret(Windows::Data::Json::JsonObject const& message);
        void ForwardKey(Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args,
                        bool down);
        void SizeEmacsFrame();

        // Runs on the UI thread.
        void OnMessage(std::string const& line);
        void Screen(Windows::Data::Json::JsonObject const& message);
        void Measure(Windows::Data::Json::JsonObject const& message);
        void ReconcileRows(Microsoft::UI::Xaml::Controls::Panel const& panel,
                           Windows::Data::Json::JsonArray const& items);
        void AttachEvents(Microsoft::UI::Xaml::FrameworkElement const& root,
                          Windows::Data::Json::JsonArray const& events);

        void Call(Windows::Data::Json::JsonObject const& message);
        void SendHostEvent(winrt::hstring const& name,
                           Windows::Data::Json::JsonObject const& details);

        // Safe from any thread.
        void SendEvent(int64_t id, Windows::Data::Json::JsonObject const& args);
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
        // How big Emacs was last told its frame is, so that it is only
        // told once.
        SIZE m_emacsSize{ 0, 0 };
        bool m_attached{ false };
        bool m_shown{ false };
        urusi::Composition m_composition;
    };
}

namespace winrt::urusi_emacs::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
