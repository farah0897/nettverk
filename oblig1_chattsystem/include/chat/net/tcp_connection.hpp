#pragma once

#include <cstddef>
#include <cstdint>

#include <span>

namespace chat {

/// Én etablert TCP-forbindelse (én socket). RAII; flyttbar, ikke kopierbar.
class TcpConnection {
public:
    TcpConnection() = default;
    explicit TcpConnection(int fd) : fd_{fd} {}

    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    void close();

    [[nodiscard]] int fd() const { return fd_; }
    [[nodiscard]] bool is_open() const { return fd_ >= 0; }

    /// Sender hele bufferen (håndterer delvise `send`).
    bool send_all(std::span<const std::byte> data);

    /// Én `recv`-operasjon; `out_count` settes til antall bytes. `0` = peer har lukket.
    bool recv_some(std::span<std::byte> buffer, std::size_t& out_count);

    bool set_non_blocking(bool enable);

    [[nodiscard]] int last_errno() const { return last_errno_; }

private:
    int fd_{-1};
    int last_errno_{0};
};

}  // namespace chat
