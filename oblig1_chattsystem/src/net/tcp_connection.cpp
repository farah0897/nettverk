#include "chat/net/tcp_connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include <cstring>

namespace chat {

TcpConnection::~TcpConnection() {
    close();
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : fd_{other.fd_}, last_errno_{other.last_errno_} {
    other.fd_ = -1;
    other.last_errno_ = 0;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        last_errno_ = other.last_errno_;
        other.fd_ = -1;
        other.last_errno_ = 0;
    }
    return *this;
}

void TcpConnection::close() {
    if (fd_ >= 0) {
        if (::close(fd_) != 0) {
            last_errno_ = errno;
        }
        fd_ = -1;
    }
}

bool TcpConnection::send_all(std::span<const std::byte> data) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    const auto* p = reinterpret_cast<const char*>(data.data());
    std::size_t total = data.size();
    if (total > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())) {
        last_errno_ = EMSGSIZE;
        return false;
    }
    std::size_t sent = 0;
    while (sent < total) {
        ssize_t r = 0;
        do {
            r = ::send(fd_, p + sent, static_cast<ssize_t>(total - sent), MSG_NOSIGNAL);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
            last_errno_ = errno;
            return false;
        }
        if (r == 0) {
            last_errno_ = ECONNRESET;
            return false;
        }
        sent += static_cast<std::size_t>(r);
    }
    last_errno_ = 0;
    return true;
}

bool TcpConnection::recv_some(std::span<std::byte> buffer, std::size_t& out_count) {
    out_count = 0;
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    if (buffer.empty()) {
        last_errno_ = 0;
        return true;
    }
    auto* p = reinterpret_cast<char*>(buffer.data());
    const auto max_recv = std::min(buffer.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
    ssize_t r = 0;
    do {
        r = ::recv(fd_, p, static_cast<ssize_t>(max_recv), 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        last_errno_ = errno;
        return false;
    }
    out_count = static_cast<std::size_t>(r);
    last_errno_ = 0;
    return true;
}

bool TcpConnection::set_non_blocking(bool enable) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return false;
    }
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        last_errno_ = errno;
        return false;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    if (::fcntl(fd_, F_SETFL, flags) != 0) {
        last_errno_ = errno;
        return false;
    }
    last_errno_ = 0;
    return true;
}

}  // namespace chat
