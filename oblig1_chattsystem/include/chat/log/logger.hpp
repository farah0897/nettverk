#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct sockaddr_in;

namespace chat {

enum class LogLevel : std::uint8_t { Info = 0, Warn = 1, Error = 2 };

/// Enkel logger til stderr. Serialiser med `cout_mutex` ved behov.
class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel lvl);
    LogLevel level() const;

    void info(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);

    // Nettverk (kort logglinje)
    void sent_broadcast(std::string_view what, std::uint16_t port, std::size_t bytes);
    void recv_broadcast(std::string_view what, const sockaddr_in& from, std::size_t bytes);
    void sent_multicast(std::string_view what, std::string_view group_ip, std::uint16_t port,
                        std::size_t bytes);
    void recv_multicast(std::string_view what, const sockaddr_in& from, std::size_t bytes);
    void sent_unicast(std::string_view what, const sockaddr_in& to, std::size_t bytes);
    void recv_unicast(std::string_view what, const sockaddr_in& from, std::size_t bytes);

    void join_group(std::string_view group_ip, std::uint16_t port);
    void leave_group(std::string_view group_ip, std::uint16_t port);

    void parsing_error(std::string_view context);
    void socket_error(std::string_view context, int err_no);

    /// Ugyldig/ for stor UDP-pakke; droppes.
    void dropped_malformed_datagram(std::string_view reason, std::size_t byte_count);
    void network_recoverable(std::string_view context, int err_no);
    void shutdown_step(std::string_view step);

private:
    Logger() = default;
    void log(LogLevel lvl, std::string_view msg);

    LogLevel level_{LogLevel::Info};
};

}  // namespace chat

