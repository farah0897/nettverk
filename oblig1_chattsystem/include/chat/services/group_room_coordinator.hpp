#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>

namespace chat {

class UdpSocket;
struct SecpMessage;

/// Annonseringer mottatt på broadcast; fjernes når de ikke har kommet på en stund.
struct AdvertisedGroupRoom {
    // RFC: rooms identified by room_name; kept field for backwards compatibility with UI/docs.
    std::string room_id;
    std::string room_name;
    std::string multicast_ip;
    std::uint16_t multicast_port{0};
    std::chrono::steady_clock::time_point last_advert{};
};

inline constexpr std::chrono::seconds kGroupAdvertBroadcastInterval{10};
inline constexpr std::chrono::seconds kGroupRegistryStaleAge{25};

/// Multicast-grupperom + annonsering på `kGroupAdvertBroadcastPort`.
class GroupRoomCoordinator {
public:
    GroupRoomCoordinator(UdpSocket& advert_broadcast_socket, in_addr local_interface,
                           std::string username);

    using GroupMessageHandler =
        std::function<void(const std::string& room_name, const std::string& sender,
                           const std::string& text)>;

    void set_on_group_message(GroupMessageHandler h);

    /// Opprett åpent rom (RFC: annonseres via ROOM_ANNOUNCE, chat via multicast).
    bool create_room(std::string room_display_name);

    std::vector<AdvertisedGroupRoom> list_advertised_rooms();

    /// Kun én aktiv gruppe om gangen; allerede medlem i samme rom → false.
    bool join_room(const std::string& room_name);
    void leave_room();

    bool send_group_chat(const std::string& text);

    [[nodiscard]] bool has_active_membership() const;
    [[nodiscard]] std::optional<std::string> active_room_id() const;

    /// Broadcast `GroupAdvert` (kun hvis vi eier et rom).
    void broadcast_owned_advert();

    /// SECP handlers (decoded in receive loop).
    void on_room_announce(const SecpMessage& msg, const sockaddr_in& from);
    void on_room_chat(const SecpMessage& msg, const sockaddr_in& from);

    /// Multicast receive helper for central receive loop.
    /// Returns -1 if not currently joined.
    [[nodiscard]] int multicast_fd() const;
    bool recv_multicast(std::span<std::byte> buffer, sockaddr_in& from, std::size_t& out_len);

private:
    struct OwnedRoom {
        std::string room_name;
    };

    void prune_registry_unlocked();
    void leave_multicast_membership_unlocked();
    [[nodiscard]] bool bind_multicast_channel(const in_addr& group, std::uint16_t port,
                                              std::string& err_out);

    UdpSocket* advert_sock_{nullptr};
    in_addr local_if_{};
    std::string username_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AdvertisedGroupRoom> registry_;
    std::optional<OwnedRoom> owned_;
    std::unique_ptr<UdpSocket> mcast_sock_;
    std::string active_room_name_;
    in_addr joined_group_{};
    std::uint16_t joined_port_{0};
    bool joined_membership_{false};

    GroupMessageHandler on_group_msg_;
};

}  // namespace chat
