#include "chat/services/discovery_service.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/state/user_directory.hpp"

#include <arpa/inet.h>

#include <cstring>

namespace chat {

DiscoveryService::DiscoveryService(UserDirectory& users, UdpSocket& socket)
    : users_{&users}, socket_{&socket} {
    broadcast_dest_.sin_family = AF_INET;
    broadcast_dest_.sin_port = htons(kUdpPort);
    broadcast_dest_.sin_addr.s_addr = htonl(INADDR_BROADCAST);
}

void DiscoveryService::set_identity(std::string username, const in_addr& local_ipv4) {
    username_ = std::move(username);
    std::memcpy(ipv4_.data(), &local_ipv4.s_addr, ipv4_.size());
    identity_ready_ = !username_.empty();
}

bool DiscoveryService::ipv4_announced_matches_sender(const std::array<std::uint8_t, 4>& announced,
                                                     const sockaddr_in& from) const {
    return std::memcmp(announced.data(), &from.sin_addr, sizeof(from.sin_addr)) == 0;
}

bool DiscoveryService::send_presence_broadcast() {
    if (!identity_ready_ || !socket_) {
        return false;
    }
    in_addr a{};
    std::memcpy(&a.s_addr, ipv4_.data(), ipv4_.size());
    char ipbuf[INET_ADDRSTRLEN];
    const char* ip = ::inet_ntop(AF_INET, &a, ipbuf, sizeof(ipbuf));
    const std::string line = build_secp_line(SecpType::Presence, "-", username_, ip ? ip : "-");
    if (line.empty()) {
        Logger::instance().parsing_error("build PRESENCE");
        return false;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};
    const bool ok = socket_->send_to(bytes, broadcast_dest_);
    if (ok) {
        Logger::instance().sent_broadcast("PRESENCE", kUdpPort, bytes.size());
    } else {
        Logger::instance().socket_error("send Presence broadcast", socket_->last_errno());
    }
    return ok;
}

void DiscoveryService::on_presence(const std::string& username, const std::string& ip_payload,
                                  const sockaddr_in& from) {
    if (username.empty() || username.size() > kMaxUsernameBytes) {
        return;
    }
    // RFC presence payload is ip-address. We additionally validate it matches sender IP.
    char frombuf[INET_ADDRSTRLEN];
    const char* from_ip = ::inet_ntop(AF_INET, &from.sin_addr, frombuf, sizeof(frombuf));
    if (from_ip == nullptr) {
        return;
    }
    if (!ip_payload.empty() && ip_payload != "-" && ip_payload != from_ip) {
        return;
    }
    users_->upsert(username, from);
}

void DiscoveryService::prune_stale_peers() {
    users_->prune_stale(kDiscoveryPeerStaleAge);
}

}  // namespace chat
