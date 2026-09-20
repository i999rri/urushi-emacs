#pragma once

#include "MainWindow.g.h"
#include "EmacsHost.h"
#include "UiServer.h"

#include <memory>
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

        // Starts listening for Emacs once the named elements (Surface, Status)
        // exist, which is only after the generated InitializeComponent.
        void InitializeComponent();

    private:
        void Start(uint16_t port);
        void StartEmacs();

        // Runs on the UI thread.
        void OnMessage(std::string const& line);
        void Render(Windows::Data::Json::JsonObject const& message);
        void AttachEvents(Microsoft::UI::Xaml::FrameworkElement const& root,
                          Windows::Data::Json::JsonArray const& events);

        // Safe from any thread.
        void SendEvent(int64_t id, Windows::Data::Json::JsonObject const& args);
        void SendError(winrt::hstring const& message);
        void Send(Windows::Data::Json::JsonObject const& message);

        void ShowStatus(winrt::hstring const& text);
        void AppendLog(std::string const& text);

        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::unique_ptr<urusi::UiServer> m_server;
        std::string m_log;
    };
}

namespace winrt::urusi_emacs::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
