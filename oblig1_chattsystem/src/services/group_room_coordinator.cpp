#include "chat/services/group_room_coordinator.hpp"

#include "chat/net/local_endpoint.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/util/string_trim.hpp"

#include "chat/log/logger.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <optional>
#include <span>

namespace chat {

namespace {

}  // namespace

GroupRoomCoordinator::GroupRoomCoordinator(UdpSocket& advert_broadcast_socket, in_addr local_interface,
                                           std::string username)
    : advert_sock_{&advert_broadcast_socket}, local_if_{local_interface}, username_{std::move(
                                                                                   username)} {}

void GroupRoomCoordinator::set_on_group_message(GroupMessageHandler h) {
    std::lock_guard lock{mutex_};
    on_group_msg_ = std::move(h);
}

void GroupRoomCoordinator::prune_registry_unlocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = registry_.begin(); it != registry_.end();) {
        if (now - it->second.last_advert > kGroupRegistryStaleAge) {
            it = registry_.erase(it);
        } else {
            ++it;
        }
    }
}

void GroupRoomCoordinator::leave_multicast_membership_unlocked() {
    if (mcast_sock_ && mcast_sock_->fd() >= 0 && joined_membership_) {
        Logger::instance().leave_group(format_ipv4(joined_group_), joined_port_);
        if (!mcast_sock_->multicast_drop_membership(joined_group_, local_if_)) {
            Logger::instance().socket_error("IP_DROP_MEMBERSHIP", mcast_sock_->last_errno());
        }
    }
    joined_membership_ = false;
    joined_port_ = 0;
    mcast_sock_.reset();
}

bool GroupRoomCoordinator::bind_multicast_channel(const in_addr& group, std::uint16_t port,
                                                  std::string& err_out) {
    leave_multicast_membership_unlocked();

    auto sk = std::make_unique<UdpSocket>();
    if (!sk->open()) {
        err_out = "socket";
        return false;
    }
    if (!sk->set_reuse_address(true)) {
        err_out = "SO_REUSEADDR";
        return false;
    }
    if (!sk->bind(port)) {
        err_out = "bind";
        return false;
    }
    if (!sk->set_multicast_outbound_interface(local_if_)) {
        err_out = "IP_MULTICAST_IF";
        return false;
    }
    if (!sk->multicast_add_membership(group, local_if_)) {
        err_out = "IP_ADD_MEMBERSHIP";
        return false;
    }
    Logger::instance().join_group(format_ipv4(group), port);
    if (!sk->set_multicast_ttl(1)) {
        err_out = "IP_MULTICAST_TTL";
        return false;
    }
    if (!sk->set_multicast_loop(true)) {
        err_out = "IP_MULTICAST_LOOP";
        return false;
    }

    mcast_sock_ = std::move(sk);
    joined_group_ = group;
    joined_port_ = port;
    joined_membership_ = true;
    return true;
}

bool GroupRoomCoordinator::create_room(std::string room_display_name) {
    trim_in_place(room_display_name);
    if (room_display_name.empty() || room_display_name.size() > kMaxRoomNameBytes) {
        return false;
    }

    in_addr group{};
    if (::inet_pton(AF_INET, kOpenRoomMulticastGroup, &group) != 1) {
        return false;
    }
    std::string err;
    {
        std::lock_guard lock{mutex_};
        leave_multicast_membership_unlocked();
        active_room_name_.clear();
        owned_.reset();
        if (!bind_multicast_channel(group, kUdpPort, err)) {
            return false;
        }
        OwnedRoom owned;
        owned.room_name = room_display_name;
        owned_ = std::move(owned);
        active_room_name_ = room_display_name;
    }
    broadcast_owned_advert();
    return true;
}

std::vector<AdvertisedGroupRoom> GroupRoomCoordinator::list_advertised_rooms() {
    std::lock_guard lock{mutex_};
    prune_registry_unlocked();
    std::vector<AdvertisedGroupRoom> out;
    out.reserve(registry_.size());
    for (const auto& [k, v] : registry_) {
        (void)k;
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const AdvertisedGroupRoom& a, const AdvertisedGroupRoom& b) {
        return a.room_name < b.room_name;
    });
    return out;
}

bool GroupRoomCoordinator::join_room(const std::string& room_name) {
    if (room_name.empty()) {
        return false;
    }
    if (room_name.size() > kMaxRoomNameBytes) {
        return false;
    }
    std::lock_guard lock{mutex_};
    prune_registry_unlocked();
    if (active_room_name_ == room_name) {
        return false;
    }
    const auto it = registry_.find(room_name);
    if (it == registry_.end()) {
        return false;
    }
    in_addr group{};
    if (::inet_pton(AF_INET, kOpenRoomMulticastGroup, &group) != 1) {
        return false;
    }
    if (owned_ && owned_->room_name != room_name) {
        owned_.reset();
    }

    std::string err;
    leave_multicast_membership_unlocked();
    if (!bind_multicast_channel(group, kUdpPort, err)) {
        return false;
    }
    active_room_name_ = room_name;
    return true;
}

void GroupRoomCoordinator::leave_room() {
    std::lock_guard lock{mutex_};
    const std::string left = active_room_name_;
    leave_multicast_membership_unlocked();
    active_room_name_.clear();
    if (owned_ && owned_->room_name == left) {
        owned_.reset();
    }
}

bool GroupRoomCoordinator::send_group_chat(const std::string& text) {
    std::string trimmed = text;
    trim_in_place(trimmed);
    if (trimmed.empty() || trimmed.size() > kMaxChatMessageBytes) {
        return false;
    }

    std::string room_name;
    std::string sender_username;
    in_addr group_addr{};
    std::uint16_t group_port = 0;
    UdpSocket* socket = nullptr;
    {
        std::lock_guard lock{mutex_};
        if (active_room_name_.empty() || !mcast_sock_) {
            return false;
        }
        room_name = active_room_name_;
        sender_username = username_;
        group_addr = joined_group_;
        group_port = joined_port_;
        socket = mcast_sock_.get();
    }

    const std::string line = build_secp_line(SecpType::Chat, room_name, sender_username, trimmed);
    if (line.empty()) {
        return false;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr = group_addr;
    dest.sin_port = htons(group_port);

    const bool ok = socket->send_to(bytes, dest);
    if (ok) {
        Logger::instance().sent_multicast("GroupChat", format_ipv4(group_addr), group_port,
                                          bytes.size());
    } else {
        Logger::instance().socket_error("send GroupChat multicast", socket->last_errno());
    }
    return ok;
}

bool GroupRoomCoordinator::has_active_membership() const {
    std::lock_guard lock{mutex_};
    return !active_room_name_.empty() && mcast_sock_ != nullptr;
}

std::optional<std::string> GroupRoomCoordinator::active_room_id() const {
    std::lock_guard lock{mutex_};
    if (active_room_name_.empty()) {
        return std::nullopt;
    }
    return active_room_name_;
}

void GroupRoomCoordinator::broadcast_owned_advert() {
    OwnedRoom owned;
    {
        std::lock_guard lock{mutex_};
        if (!owned_) {
            return;
        }
        owned = *owned_;
    }

    // RFC: ROOM_ANNOUNCE|room-name|owner|OPEN
    const std::string line = build_secp_line(SecpType::RoomAnnounce, owned.room_name, username_, "OPEN");
    if (line.empty() || advert_sock_ == nullptr) {
        return;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kUdpPort);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const bool ok = advert_sock_->send_to(bytes, dest);
    if (ok) {
        Logger::instance().sent_broadcast("ROOM_ANNOUNCE", kUdpPort, bytes.size());
    } else {
        Logger::instance().socket_error("send GroupAdvert broadcast", advert_sock_->last_errno());
    }
}

void GroupRoomCoordinator::on_room_announce(const SecpMessage& msg, const sockaddr_in&) {
    // ROOM_ANNOUNCE|room-name|owner|OPEN
    if (msg.room.empty() || msg.room == "-" || msg.username.empty()) {
        return;
    }
    if (msg.payload != "OPEN") {
        return;
    }
    std::lock_guard lock{mutex_};
    AdvertisedGroupRoom gr;
    gr.room_id = msg.room;  // kept for compatibility with struct; equals room_name now
    gr.room_name = msg.room;
    gr.multicast_ip = kOpenRoomMulticastGroup;
    gr.multicast_port = kUdpPort;
    gr.last_advert = std::chrono::steady_clock::now();
    registry_[gr.room_name] = std::move(gr);
    prune_registry_unlocked();
}

void GroupRoomCoordinator::on_room_chat(const SecpMessage& msg, const sockaddr_in& from) {
    if (msg.type != SecpType::Chat) {
        return;
    }
    std::string expect_room;
    GroupMessageHandler cb;
    std::string self_user;
    {
        std::lock_guard lock{mutex_};
        if (active_room_name_.empty()) {
            return;
        }
        expect_room = active_room_name_;
        cb = on_group_msg_;
        self_user = username_;
    }
    if (msg.room != expect_room) {
        return;
    }
    if (msg.username.empty() || msg.payload.empty()) {
        return;
    }
    if (msg.username == self_user) {
        return;
    }
    if (cb) {
        Logger::instance().recv_multicast("CHAT(open room)", from, msg.payload.size());
        cb(expect_room, msg.username, msg.payload);
    }
}

int GroupRoomCoordinator::multicast_fd() const {
    std::lock_guard lock{mutex_};
    if (!mcast_sock_) {
        return -1;
    }
    return mcast_sock_->fd();
}

bool GroupRoomCoordinator::recv_multicast(std::span<std::byte> buffer, sockaddr_in& from,
                                         std::size_t& out_len) {
    UdpSocket* sk = nullptr;
    {
        std::lock_guard lock{mutex_};
        sk = mcast_sock_.get();
    }
    if (sk == nullptr) {
        out_len = 0;
        return false;
    }
    return sk->recv_from(buffer, from, out_len);
}

}  // namespace chat
