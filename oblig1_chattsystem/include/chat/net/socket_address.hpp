#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace chat {

/// Tynn wrapper rundt `sockaddr_in` + hjelpefunksjoner (parse/print, broadcast/multicast).
class SocketAddress {
public:
    static bool from_ipv4_port(std::string_view ip, std::uint16_t port, sockaddr_in& out);
    static std::string to_string(const sockaddr_in& addr);

    sockaddr_in storage{};
};

}  // namespace chat
