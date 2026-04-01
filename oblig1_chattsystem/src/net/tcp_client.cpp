#include "chat/net/tcp_client.hpp"

#include "chat/log/logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace chat {

thread_local int TcpClient::last_errno_{0};

std::optional<TcpConnection> TcpClient::connect_ipv4(std::string_view host, std::uint16_t port) {
    last_errno_ = 0;
    std::string host_buf{host};
    in_addr addr{};
    if (::inet_pton(AF_INET, host_buf.c_str(), &addr) != 1) {
        last_errno_ = EINVAL;
        Logger::instance().warn("TCP connect: invalid IPv4 host string");
        return std::nullopt;
    }
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr = addr;
    sa.sin_port = htons(port);
    return connect(sa);
}

std::optional<TcpConnection> TcpClient::connect(const sockaddr_in& addr) {
    last_errno_ = 0;
    if (addr.sin_family != AF_INET) {
        last_errno_ = EAFNOSUPPORT;
        Logger::instance().warn("TCP connect: address not AF_INET");
        return std::nullopt;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP socket()", last_errno_);
        return std::nullopt;
    }
    int r = 0;
    do {
        r = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    } while (r != 0 && errno == EINTR);
    if (r != 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP connect()", last_errno_);
        ::close(fd);
        return std::nullopt;
    }
    return TcpConnection{fd};
}

}  // namespace chat
