#pragma once

#include "chat/net/tcp_connection.hpp"

#include <cstdint>
#include <optional>

#include <netinet/in.h>

namespace chat {

/// TCP-lytter (IPv4). `listen_on` → `accept` gir `TcpConnection`.
class TcpServer {
public:
    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&& other) noexcept;
    TcpServer& operator=(TcpServer&& other) noexcept;

    void close();

    [[nodiscard]] int fd() const { return fd_; }
    [[nodiscard]] bool is_open() const { return fd_ >= 0; }
    [[nodiscard]] int last_errno() const { return last_errno_; }

    /// `SO_REUSEADDR`, `bind(INADDR_ANY, port)`, `listen(backlog)`.
    bool listen_on(std::uint16_t port, int backlog = 32);

    /// Blokkerende `accept` med valgfri peer-adresse.
    std::optional<TcpConnection> accept_peer(sockaddr_in* peer_out = nullptr);

    bool set_non_blocking(bool enable);

private:
    int fd_{-1};
    int last_errno_{0};
};

}  // namespace chat
