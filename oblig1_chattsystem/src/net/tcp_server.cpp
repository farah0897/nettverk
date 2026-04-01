#include "chat/net/tcp_server.hpp"

#include "chat/log/logger.hpp"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace chat {

TcpServer::~TcpServer() {
    close();
}

TcpServer::TcpServer(TcpServer&& other) noexcept : fd_{other.fd_}, last_errno_{other.last_errno_} {
    other.fd_ = -1;
    other.last_errno_ = 0;
}

TcpServer& TcpServer::operator=(TcpServer&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        last_errno_ = other.last_errno_;
        other.fd_ = -1;
        other.last_errno_ = 0;
    }
    return *this;
}

void TcpServer::close() {
    if (fd_ >= 0) {
        if (::close(fd_) != 0) {
            last_errno_ = errno;
        }
        fd_ = -1;
    }
}

bool TcpServer::listen_on(std::uint16_t port, int backlog) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP server socket()", last_errno_);
        return false;
    }
    int on = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP server SO_REUSEADDR", last_errno_);
        close();
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP server bind()", last_errno_);
        close();
        return false;
    }
    if (::listen(fd_, backlog) != 0) {
        last_errno_ = errno;
        Logger::instance().socket_error("TCP server listen()", last_errno_);
        close();
        return false;
    }
    last_errno_ = 0;
    return true;
}

std::optional<TcpConnection> TcpServer::accept_peer(sockaddr_in* peer_out) {
    if (fd_ < 0) {
        last_errno_ = EBADF;
        return std::nullopt;
    }
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    int cfd = -1;
    do {
        cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    } while (cfd < 0 && errno == EINTR);
    if (cfd < 0) {
        last_errno_ = errno;
        if (last_errno_ != EAGAIN && last_errno_ != EWOULDBLOCK) {
            Logger::instance().socket_error("TCP accept()", last_errno_);
        }
        return std::nullopt;
    }
    if (peer.sin_family != AF_INET) {
        ::close(cfd);
        last_errno_ = EAFNOSUPPORT;
        return std::nullopt;
    }
    if (peer_out != nullptr) {
        *peer_out = peer;
    }
    last_errno_ = 0;
    return TcpConnection{cfd};
}

bool TcpServer::set_non_blocking(bool enable) {
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
