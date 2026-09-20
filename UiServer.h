#pragma once

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace urusi
{
    // Talks to Emacs over one TCP connection on the loopback interface, one
    // JSON message per line. Binding to 127.0.0.1 only keeps the port away
    // from other machines; any local process can still connect.
    //
    // Messages arrive on the server's own thread. The owner is responsible for
    // hopping to the UI thread before touching XAML.
    class UiServer
    {
    public:
        using MessageHandler = std::function<void(std::string line)>;
        using DisconnectHandler = std::function<void()>;

        UiServer(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect);
        ~UiServer();

        UiServer(UiServer const&) = delete;
        UiServer& operator=(UiServer const&) = delete;

        // Sends one message. Safe to call from any thread; dropped silently
        // when no Emacs is connected.
        void Send(std::string const& line);

        uint16_t Port() const noexcept { return m_port; }

    private:
        void Run();
        void ReadFrom(SOCKET client);

        uint16_t m_port;
        MessageHandler m_onMessage;
        DisconnectHandler m_onDisconnect;

        SOCKET m_listen{ INVALID_SOCKET };
        std::mutex m_clientMutex;
        SOCKET m_client{ INVALID_SOCKET };
        std::atomic<bool> m_stopping{ false };
        std::thread m_thread;
    };
}
