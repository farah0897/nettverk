#include "chat/log/logger.hpp"

#include "chat/app/sync_output.hpp"
#include "chat/net/errno_util.hpp"

#include <arpa/inet.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace chat {

namespace {

std::string ipv4_port_string(const sockaddr_in& a) {
    char buf[INET_ADDRSTRLEN];
    const char* ip = ::inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
    std::ostringstream o;
    o << (ip ? ip : "?") << ":" << ntohs(a.sin_port);
    return o.str();
}

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "INFO";
    }
}

std::string timestamp_hhmmss() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream o;
    o << std::setfill('0') << std::setw(2) << tm.tm_hour << ":" << std::setw(2) << tm.tm_min
      << ":" << std::setw(2) << tm.tm_sec;
    return o.str();
}

}  // namespace

Logger& Logger::instance() {
    static Logger g;
    return g;
}

void Logger::set_level(LogLevel lvl) {
    level_ = lvl;
}

LogLevel Logger::level() const {
    return level_;
}

void Logger::log(LogLevel lvl, std::string_view msg) {
    if (static_cast<int>(lvl) < static_cast<int>(level_)) {
        return;
    }
    std::lock_guard lock{chat::cout_mutex};
    std::clog << "[" << timestamp_hhmmss() << "] [" << level_name(lvl) << "] " << msg << "\n"
              << std::flush;
}

void Logger::info(std::string_view msg) { log(LogLevel::Info, msg); }
void Logger::warn(std::string_view msg) { log(LogLevel::Warn, msg); }
void Logger::error(std::string_view msg) { log(LogLevel::Error, msg); }

void Logger::sent_broadcast(std::string_view what, std::uint16_t port, std::size_t bytes) {
    std::ostringstream o;
    o << "send broadcast: " << what << " port=" << port << " bytes=" << bytes;
    info(o.str());
}

void Logger::recv_broadcast(std::string_view what, const sockaddr_in& from, std::size_t bytes) {
    std::ostringstream o;
    o << "recv broadcast: " << what << " from=" << ipv4_port_string(from) << " bytes=" << bytes;
    info(o.str());
}

void Logger::sent_multicast(std::string_view what, std::string_view group_ip, std::uint16_t port,
                            std::size_t bytes) {
    std::ostringstream o;
    o << "send multicast: " << what << " to=" << group_ip << ":" << port << " bytes=" << bytes;
    info(o.str());
}

void Logger::recv_multicast(std::string_view what, const sockaddr_in& from, std::size_t bytes) {
    std::ostringstream o;
    o << "recv multicast: " << what << " from=" << ipv4_port_string(from) << " bytes=" << bytes;
    info(o.str());
}

void Logger::sent_unicast(std::string_view what, const sockaddr_in& to, std::size_t bytes) {
    std::ostringstream o;
    o << "send unicast: " << what << " to=" << ipv4_port_string(to) << " bytes=" << bytes;
    info(o.str());
}

void Logger::recv_unicast(std::string_view what, const sockaddr_in& from, std::size_t bytes) {
    std::ostringstream o;
    o << "recv unicast: " << what << " from=" << ipv4_port_string(from) << " bytes=" << bytes;
    info(o.str());
}

void Logger::join_group(std::string_view group_ip, std::uint16_t port) {
    std::ostringstream o;
    o << "multicast join: " << group_ip << ":" << port;
    info(o.str());
}

void Logger::leave_group(std::string_view group_ip, std::uint16_t port) {
    std::ostringstream o;
    o << "multicast leave: " << group_ip << ":" << port;
    info(o.str());
}

void Logger::parsing_error(std::string_view context) {
    std::ostringstream o;
    o << "parsing error: " << context;
    warn(o.str());
}

void Logger::socket_error(std::string_view context, int err_no) {
    std::ostringstream o;
    o << "socket error: " << context << " — errno=" << err_no << " (" << errno_string(err_no) << ")";
    error(o.str());
}

}  // namespace chat

