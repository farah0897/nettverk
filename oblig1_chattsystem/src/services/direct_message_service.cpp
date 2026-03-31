#include "chat/services/direct_message_service.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/state/user_directory.hpp"
#include "chat/util/string_trim.hpp"

#include <arpa/inet.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <span>

namespace chat {

namespace {

sockaddr_in make_unicast_dest(const sockaddr_in& peer, std::uint16_t port) {
    sockaddr_in d = peer;
    d.sin_port = htons(port);
    return d;
}

}  // namespace

DirectMessageService::DirectMessageService(UdpSocket& socket, UserDirectory& users,
                                           std::string self_username)
    : socket_{&socket}, users_{&users}, self_username_{std::move(self_username)} {}

void DirectMessageService::set_on_private_message(
    std::function<void(const std::string& session_id, const std::string& sender,
                       const std::string& text)> cb) {
    std::lock_guard lock{mutex_};
    on_private_ = std::move(cb);
}

void DirectMessageService::set_on_invite(
    std::function<void(const std::string& session_id, const std::string& from_username)> cb) {
    std::lock_guard lock{mutex_};
    on_invite_ = std::move(cb);
}

void DirectMessageService::set_on_status(std::function<void(const std::string& msg)> cb) {
    std::lock_guard lock{mutex_};
    on_status_ = std::move(cb);
}

void DirectMessageService::status(const std::string& msg) const {
    std::function<void(const std::string&)> cb;
    {
        std::lock_guard lock{mutex_};
        cb = on_status_;
    }
    if (cb) {
        cb(msg);
    }
}

std::string DirectMessageService::sockaddr_ipv4_string(const sockaddr_in& a) {
    char buf[INET_ADDRSTRLEN];
    if (::inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return std::string{buf};
}

bool DirectMessageService::same_endpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
           std::memcmp(&a.sin_addr, &b.sin_addr, sizeof(in_addr)) == 0;
}

std::string DirectMessageService::make_session_id() {
    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<unsigned> d(0, 0xFFFFFFU);
    char out[7];
    std::snprintf(out, sizeof(out), "%06x", d(gen));
    return std::string{out};
}

std::optional<std::string> DirectMessageService::invite_user(const std::string& target_username) {
    if (socket_ == nullptr || users_ == nullptr) {
        return std::nullopt;
    }
    std::string target = target_username;
    trim_in_place(target);
    if (target.empty() || target.size() > kMaxUsernameBytes || target == self_username_) {
        return std::nullopt;
    }

    const auto addr_opt = users_->find_address(target);
    if (!addr_opt) {
        status("Fant ikke bruker \"" + target + "\" i aktiv bruker-liste.");
        return std::nullopt;
    }
    const sockaddr_in dest = make_unicast_dest(*addr_opt, kUdpPort);

    const std::string sid = make_session_id();
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock{mutex_};
        if (sessions_.contains(sid)) {
            return std::nullopt;
        }
        Session s;
        s.state = DirectSessionState::OutgoingPending;
        s.session_id = sid;
        s.peer_username = target;
        s.peer_addr = dest;
        s.created_at = now;
        s.last_sent = now;
        s.last_update = now;
        sessions_[sid] = std::move(s);
    }

    // RFC: INVITE|room-name|owner|CLOSED;to=invitedUser
    const std::string payload = std::string{"CLOSED;to="} + target;
    const std::string line = build_secp_line(SecpType::Invite, sid, self_username_, payload);
    if (line.empty()) {
        return std::nullopt;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};
    if (!socket_->send_to(bytes, dest)) {
        Logger::instance().socket_error("send INVITE unicast", socket_->last_errno());
        status("Kunne ikke sende invitasjon (sendto feilet).");
        return std::nullopt;
    }
    Logger::instance().sent_unicast("INVITE(CLOSED)", dest, bytes.size());
    status("Sendte invitasjon til \"" + target + "\" (session " + sid + ").");
    return sid;
}

bool DirectMessageService::accept_invite(const std::string& room_name) {
    std::string room = room_name;
    trim_in_place(room);
    if (room.empty() || room.size() > kMaxRoomNameBytes) {
        return false;
    }
    std::string peer_user;
    {
        std::lock_guard lock{mutex_};
        auto it = sessions_.find(room);
        if (it == sessions_.end() || it->second.state != DirectSessionState::IncomingPending) {
            return false;
        }
        it->second.state = DirectSessionState::Active;
        it->second.last_update = std::chrono::steady_clock::now();
        peer_user = it->second.peer_username;
    }
    status("Aksepterte INVITE fra \"" + peer_user + "\" (rom " + room + ").");
    return true;
}

bool DirectMessageService::decline_invite(const std::string& room_name, const std::string& reason) {
    std::string room = room_name;
    trim_in_place(room);
    if (room.empty() || room.size() > kMaxRoomNameBytes) {
        return false;
    }
    std::string rs = reason;
    trim_in_place(rs);
    if (rs.size() > kMaxChatMessageBytes) {
        rs.resize(kMaxChatMessageBytes);
    }

    std::string peer_user;
    {
        std::lock_guard lock{mutex_};
        auto it = sessions_.find(room);
        if (it == sessions_.end() || it->second.state != DirectSessionState::IncomingPending) {
            return false;
        }
        peer_user = it->second.peer_username;
        sessions_.erase(it);
    }

    if (!rs.empty()) {
        status("Avslo INVITE fra \"" + peer_user + "\" (rom " + room + "). Grunn: " + rs);
    } else {
        status("Avslo INVITE fra \"" + peer_user + "\" (rom " + room + ").");
    }
    return true;
}

bool DirectMessageService::send_private_chat(const std::string& session_id, const std::string& text) {
    if (socket_ == nullptr) {
        return false;
    }
    std::string sid = session_id;
    trim_in_place(sid);
    if (sid.size() > 32) {
        return false;
    }
    std::string msg = text;
    trim_in_place(msg);
    if (sid.empty() || msg.empty() || msg.size() > kMaxChatMessageBytes) {
        return false;
    }

    sockaddr_in peer{};
    {
        std::lock_guard lock{mutex_};
        const auto it = sessions_.find(sid);
        if (it == sessions_.end() || it->second.state != DirectSessionState::Active) {
            return false;
        }
        peer = it->second.peer_addr;
    }

    const std::string line = build_secp_line(SecpType::Chat, sid, self_username_, msg);
    if (line.empty()) {
        return false;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};
    const bool ok = socket_->send_to(bytes, peer);
    if (ok) {
        Logger::instance().sent_unicast("CHAT(CLOSED)", peer, bytes.size());
    } else {
        Logger::instance().socket_error("send DirectChat unicast", socket_->last_errno());
    }
    return ok;
}

void DirectMessageService::on_invite(const SecpMessage& msg, const sockaddr_in& from) {
    // INVITE|room-name|owner|CLOSED;to=invitedUser
    if (msg.type != SecpType::Invite) {
        return;
    }
    if (msg.room.empty() || msg.room == "-" || msg.username.empty() || msg.payload.empty()) {
        return;
    }
    if (msg.payload.rfind("CLOSED;to=", 0) != 0) {
        return;
    }
    const std::string to = msg.payload.substr(std::string_view{"CLOSED;to="}.size());
    if (to != self_username_) {
        return;
    }

    bool inserted = false;
    {
        std::lock_guard lock{mutex_};
        if (!sessions_.contains(msg.room)) {
            Session s;
            s.state = DirectSessionState::IncomingPending;
            s.session_id = msg.room;  // room-name acts as session id in this implementation
            s.peer_username = msg.username;
            s.peer_addr = make_unicast_dest(from, kUdpPort);
            s.created_at = std::chrono::steady_clock::now();
            s.last_sent = {};
            s.last_update = std::chrono::steady_clock::now();
            sessions_[s.session_id] = s;
            inserted = true;
        }
    }
    if (inserted) {
        Logger::instance().info(std::string{"invite received (CLOSED room): room="} + msg.room +
                                " from \"" + msg.username + "\"");
        status("Innkommende INVITE (lukket rom) fra \"" + msg.username + "\". Rom: " + msg.room +
               " (bruk meny for å svare).");
    }
}

void DirectMessageService::on_room_chat(const SecpMessage& msg, const sockaddr_in& from) {
    if (msg.type != SecpType::Chat) {
        return;
    }
    if (msg.room.empty() || msg.room == "-" || msg.username.empty() || msg.payload.empty()) {
        return;
    }

    std::function<void(const std::string&, const std::string&, const std::string&)> cb;
    std::string sender;
    bool ok = false;
    {
        std::lock_guard lock{mutex_};
        const auto it = sessions_.find(msg.room);
        if (it == sessions_.end()) {
            return;
        }

        const sockaddr_in expect = make_unicast_dest(from, kUdpPort);
        if (!same_endpoint(it->second.peer_addr, expect)) {
            return;
        }
        if (!it->second.peer_username.empty() && msg.username != it->second.peer_username) {
            return;
        }

        // RFC: no explicit ACCEPT for CLOSED rooms. Treat first valid CHAT from peer
        // as implicit acceptance for inviter's outgoing invite.
        if (it->second.state == DirectSessionState::OutgoingPending) {
            it->second.state = DirectSessionState::Active;
            it->second.last_update = std::chrono::steady_clock::now();
        }

        if (it->second.state != DirectSessionState::Active) {
            return;
        }

        sender = it->second.peer_username.empty() ? msg.username : it->second.peer_username;
        cb = on_private_;
        ok = true;
    }
    if (ok && cb) {
        cb(msg.room, sender, msg.payload);
    }
}


std::vector<PendingInviteInfo> DirectMessageService::snapshot_pending_invites() const {
    std::lock_guard lock{mutex_};
    std::vector<PendingInviteInfo> out;
    for (const auto& [sid, s] : sessions_) {
        if (s.state == DirectSessionState::IncomingPending) {
            PendingInviteInfo p;
            p.session_id = sid;
            p.from_username = s.peer_username;
            p.to_username = self_username_;
            out.push_back(std::move(p));
        }
    }
    return out;
}

std::vector<ActiveDirectSessionInfo> DirectMessageService::snapshot_active_sessions() const {
    std::lock_guard lock{mutex_};
    std::vector<ActiveDirectSessionInfo> out;
    for (const auto& [sid, s] : sessions_) {
        if (s.state == DirectSessionState::Active) {
            ActiveDirectSessionInfo i;
            i.session_id = sid;
            i.peer_username = s.peer_username;
            i.peer_ip = sockaddr_ipv4_string(s.peer_addr);
            out.push_back(std::move(i));
        }
    }
    return out;
}

void DirectMessageService::prune_pending(std::chrono::seconds max_age) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired;
    {
        std::lock_guard lock{mutex_};
        for (const auto& [sid, s] : sessions_) {
            if ((s.state == DirectSessionState::IncomingPending ||
                 s.state == DirectSessionState::OutgoingPending) &&
                now - s.last_update > max_age) {
                expired.push_back(sid);
            }
        }
        for (const auto& sid : expired) {
            sessions_.erase(sid);
        }
    }
    for (const auto& sid : expired) {
        status("Session utløpt (ingen svar): " + sid);
    }
}

void DirectMessageService::resend_outgoing_pending(std::chrono::milliseconds min_interval,
                                                  std::chrono::seconds max_age) {
    if (socket_ == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();

    struct ToSend {
        std::string session_id;
        std::string peer_username;
        sockaddr_in dest{};
    };
    std::vector<ToSend> resend;
    std::vector<std::string> expired;

    {
        std::lock_guard lock{mutex_};
        for (auto& [sid, s] : sessions_) {
            if (s.state != DirectSessionState::OutgoingPending) {
                continue;
            }
            if (s.created_at.time_since_epoch().count() != 0 && (now - s.created_at) > max_age) {
                expired.push_back(sid);
                continue;
            }
            if (s.last_sent.time_since_epoch().count() != 0 && (now - s.last_sent) < min_interval) {
                continue;
            }
            s.last_sent = now;
            s.last_update = now;
            resend.push_back(ToSend{sid, s.peer_username, s.peer_addr});
        }
        for (const auto& sid : expired) {
            sessions_.erase(sid);
        }
    }

    for (const auto& it : resend) {
        const std::string payload = std::string{"CLOSED;to="} + it.peer_username;
        const std::string line = build_secp_line(SecpType::Invite, it.session_id, self_username_, payload);
        if (line.empty()) {
            Logger::instance().parsing_error("build INVITE (resend)");
            continue;
        }
        const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                               line.size()};
        if (!socket_->send_to(bytes, it.dest)) {
            Logger::instance().socket_error("resend INVITE unicast", socket_->last_errno());
        } else {
            Logger::instance().sent_unicast("INVITE(resend)", it.dest, bytes.size());
        }
    }

    for (const auto& sid : expired) {
        status("Invitasjon utløpt (ingen svar): " + sid);
    }
}

}  // namespace chat

