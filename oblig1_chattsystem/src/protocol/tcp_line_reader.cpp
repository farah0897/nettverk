#include "chat/protocol/tcp_line_reader.hpp"

#include <cerrno>

#include <array>

namespace chat {

namespace {

TcpLineReadStatus extract_line_from_pending(std::string& pending, std::string& out) {
    out.clear();
    const auto nl = pending.find('\n');
    if (nl == std::string::npos) {
        return TcpLineReadStatus::NeedMoreData;
    }
    out.assign(pending.data(), nl);
    pending.erase(0, nl + 1);
    while (!out.empty() && out.back() == '\r') {
        out.pop_back();
    }
    if (out.size() > kMaxPacketBytes) {
        return TcpLineReadStatus::Error;
    }
    return TcpLineReadStatus::LineReady;
}

}  // namespace

TcpLineReadStatus TcpLineReader::try_read_line(std::string& out) {
    out.clear();
    if (conn_ == nullptr || !conn_->is_open()) {
        return TcpLineReadStatus::Error;
    }

    for (;;) {
        TcpLineReadStatus st = extract_line_from_pending(pending_, out);
        if (st == TcpLineReadStatus::LineReady) {
            return TcpLineReadStatus::LineReady;
        }
        if (st == TcpLineReadStatus::Error) {
            return TcpLineReadStatus::Error;
        }

        if (pending_.size() > kMaxPacketBytes) {
            return TcpLineReadStatus::Error;
        }

        std::array<std::byte, 512> chunk{};
        std::size_t n = 0;
        if (!conn_->recv_some(chunk, n)) {
            if (conn_->last_errno() == EAGAIN || conn_->last_errno() == EWOULDBLOCK) {
                return TcpLineReadStatus::NeedMoreData;
            }
            return TcpLineReadStatus::Error;
        }
        if (n == 0) {
            return TcpLineReadStatus::Disconnected;
        }
        if (pending_.size() + n > kMaxPending) {
            return TcpLineReadStatus::Error;
        }
        pending_.append(reinterpret_cast<const char*>(chunk.data()), n);
    }
}

bool TcpLineReader::read_line(std::string& out) {
    out.clear();
    if (conn_ == nullptr || !conn_->is_open()) {
        return false;
    }

    for (;;) {
        const auto nl = pending_.find('\n');
        if (nl != std::string::npos) {
            out.assign(pending_.data(), nl);
            pending_.erase(0, nl + 1);
            while (!out.empty() && out.back() == '\r') {
                out.pop_back();
            }
            if (out.size() > kMaxPacketBytes) {
                return false;
            }
            return true;
        }

        if (pending_.size() > kMaxPacketBytes) {
            return false;
        }

        std::array<std::byte, 512> chunk{};
        std::size_t n = 0;
        if (!conn_->recv_some(chunk, n)) {
            return false;
        }
        if (n == 0) {
            return false;
        }
        if (pending_.size() + n > kMaxPending) {
            return false;
        }
        pending_.append(reinterpret_cast<const char*>(chunk.data()), n);
    }
}

}  // namespace chat
