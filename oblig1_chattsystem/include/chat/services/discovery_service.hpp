#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <netinet/in.h>

namespace chat {

class UserDirectory;
class UdpSocket;

/// UDP broadcast discovery: `Presence` med brukernavn + IPv4-payload (må matche avsender-IP).
inline constexpr std::chrono::seconds kDiscoveryPeerStaleAge{18};
inline constexpr std::chrono::milliseconds kDiscoveryHeartbeatInterval{10000};

class DiscoveryService {
public:
    DiscoveryService(UserDirectory& users, UdpSocket& socket);

    void set_identity(std::string username, const in_addr& local_ipv4);

    /// Sender én `Presence`-pakke til broadcast-adresse (kall ved oppstart og periodisk).
    bool send_presence_broadcast();

    /// Behandler dekodet SECP presence.
    void on_presence(const std::string& username, const std::string& ip_payload,
                     const sockaddr_in& from);

    void prune_stale_peers();

private:
    bool ipv4_announced_matches_sender(const std::array<std::uint8_t, 4>& announced,
                                       const sockaddr_in& from) const;

    UserDirectory* users_{nullptr};
    UdpSocket* socket_{nullptr};
    std::string username_;
    std::array<std::uint8_t, 4> ipv4_{};
    sockaddr_in broadcast_dest_{};
    bool identity_ready_{false};
};

}  // namespace chat
