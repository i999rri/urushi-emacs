#include "pch.h"
#include "UiServer.h"

#include <stdexcept>

#pragma comment(lib, "ws2_32.lib")

namespace urusi
{
    UiServer::UiServer(uint16_t port, MessageHandler onMessage, DisconnectHandler onDisconnect)
        : m_port(port), m_onMessage(std::move(onMessage)), m_onDisconnect(std::move(onDisconnect))
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }

        m_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listen == INVALID_SOCKET)
        {
            WSACleanup();
            throw std::runtime_error("socket failed");
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(m_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
            listen(m_listen, 1) == SOCKET_ERROR)
        {
            closesocket(m_listen);
            WSACleanup();
            throw std::runtime_error("bind/listen failed (is the port in use?)");
        }

        m_thread = std::thread([this] { Run(); });
    }

    UiServer::~UiServer()
    {
        m_stopping = true;
        // Closing the sockets unblocks accept() and recv() on the server thread.
        closesocket(m_listen);
        {
            std::lock_guard lock(m_clientMutex);
            if (m_client != INVALID_SOCKET)
            {
                closesocket(m_client);
                m_client = INVALID_SOCKET;
            }
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        WSACleanup();
    }

    void UiServer::Send(std::string const& line)
    {
        std::lock_guard lock(m_clientMutex);
        if (m_client == INVALID_SOCKET)
        {
            return;
        }

        std::string framed = line + "\n";
        char const* data = framed.data();
        int remaining = static_cast<int>(framed.size());
        while (remaining > 0)
        {
            int sent = send(m_client, data, remaining, 0);
            if (sent == SOCKET_ERROR)
            {
                return;
            }
            data += sent;
            remaining -= sent;
        }
    }

    void UiServer::Run()
    {
        while (!m_stopping)
        {
            SOCKET client = accept(m_listen, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                return;
            }

            {
                std::lock_guard lock(m_clientMutex);
                m_client = client;
            }
            ReadFrom(client);
            {
                std::lock_guard lock(m_clientMutex);
                if (m_client != INVALID_SOCKET)
                {
                    closesocket(m_client);
                    m_client = INVALID_SOCKET;
                }
            }
            if (!m_stopping)
            {
                m_onDisconnect();
            }
        }
    }

    void UiServer::ReadFrom(SOCKET client)
    {
        std::string pending;
        char buffer[4096];
        while (!m_stopping)
        {
            int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                return;
            }
            pending.append(buffer, received);

            // A message may arrive split across reads, or several in one read.
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    m_onMessage(std::move(line));
                }
            }
        }
    }
}
