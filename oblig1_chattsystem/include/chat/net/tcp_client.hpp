#pragma once

#include "chat/net/tcp_connection.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

#include <netinet/in.h>

namespace chat {

/// Oppretter utgående TCP-forbindelser (IPv4).
class TcpClient {
public:
    [[nodiscard]] static int last_errno() { return last_errno_; }

    /// Kobler til `host` (IPv4 tekst, f.eks. "192.168.1.1") og port.
    static std::optional<TcpConnection> connect_ipv4(std::string_view host, std::uint16_t port);

    /// Kobler til ferdig utfylt `AF_INET`-adresse.
    static std::optional<TcpConnection> connect(const sockaddr_in& addr);

private:
    static thread_local int last_errno_;
};

}  // namespace chat
