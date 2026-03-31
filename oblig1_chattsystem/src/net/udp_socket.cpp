#include "chat/net/udp_socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <limits>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace chat {

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : fd_{other.fd_} {
    other.fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool UdpSocket::open() {
    if (fd_ >= 0) {
        return true;
    }
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

void UdpSocket::close() {
    if (fd_ >= 0) {
        if (::close(fd_) != 0) {
            last_errno_ = errno;
        }
        fd_ = -1;
    }
}

bool UdpSocket::bind(std::uint16_t port) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::set_broadcast(bool enable) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    int on = enable ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::set_reuse_address(bool enable) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    int on = enable ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

int UdpSocket::fd() const {
    return fd_;
}

bool UdpSocket::send_to(std::span<const std::byte> data, const sockaddr_in& dest) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    const auto* p = reinterpret_cast<const char*>(data.data());
    const auto n = data.size();
    if (n > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())) {
        last_errno_ = EMSGSIZE;
        return false;
    }
    ssize_t r = 0;
    do {
        r = ::sendto(fd_, p, static_cast<ssize_t>(n), 0, reinterpret_cast<const sockaddr*>(&dest),
                     sizeof(dest));
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return r == static_cast<ssize_t>(n);
}

bool UdpSocket::recv_from(std::span<std::byte> buffer, sockaddr_in& from, std::size_t& out_len) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    auto* p = reinterpret_cast<char*>(buffer.data());
    socklen_t from_len = sizeof(from);
    ssize_t r = 0;
    do {
        r = ::recvfrom(fd_, p, static_cast<ssize_t>(buffer.size()), 0,
                       reinterpret_cast<sockaddr*>(&from), &from_len);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        out_len = 0;
        last_errno_ = errno;
        return false;
    }
    if (from.sin_family != AF_INET) {
        out_len = 0;
        last_errno_ = EAFNOSUPPORT;
        return false;
    }
    out_len = static_cast<std::size_t>(r);
    last_errno_ = 0;
    return true;
}

bool UdpSocket::multicast_add_membership(const in_addr& group, const in_addr& interface_addr) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr = group;
    mreq.imr_interface = interface_addr;
    if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::multicast_drop_membership(const in_addr& group, const in_addr& interface_addr) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr = group;
    mreq.imr_interface = interface_addr;
    if (::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::set_multicast_ttl(std::uint8_t ttl) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    auto u = static_cast<unsigned char>(ttl);
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &u, sizeof(u)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::set_multicast_loop(bool enable) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    unsigned char u = enable ? 1 : 0;
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &u, sizeof(u)) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

bool UdpSocket::set_multicast_outbound_interface(const in_addr& interface_addr) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) !=
        0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

}  // namespace chat
