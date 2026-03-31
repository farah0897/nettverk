#include "chat/services/lobby_service.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/util/string_trim.hpp"

#include <arpa/inet.h>

namespace chat {

LobbyService::LobbyService(UdpSocket& socket) : socket_{&socket} {
    broadcast_dest_.sin_family = AF_INET;
    broadcast_dest_.sin_port = htons(kUdpPort);
    broadcast_dest_.sin_addr.s_addr = htonl(INADDR_BROADCAST);
}

void LobbyService::set_on_message(
    std::function<void(const std::string& sender, const std::string& text)> cb) {
    on_message_ = std::move(cb);
}

bool LobbyService::send_chat(const std::string& username, const std::string& text) {
    if (socket_ == nullptr) {
        return false;
    }
    std::string u = username;
    std::string t = text;
    trim_in_place(u);
    trim_in_place(t);
    if (u.empty() || t.empty()) {
        return false;
    }
    if (u.size() > kMaxUsernameBytes || t.size() > kMaxChatMessageBytes) {
        return false;
    }
    // RFC global room: "USN Chat" over UDP broadcast
    const std::string line = build_secp_line(SecpType::Chat, "USN Chat", u, t);
    if (line.empty()) {
        return false;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};
    const bool ok = socket_->send_to(bytes, broadcast_dest_);
    if (ok) {
        Logger::instance().sent_broadcast("CHAT(USN Chat)", kUdpPort, bytes.size());
    } else {
        Logger::instance().socket_error("send LobbyChat broadcast", socket_->last_errno());
    }
    return ok;
}

void LobbyService::on_lobby_chat(const std::string& sender, const std::string& text) {
    if (!on_message_) {
        return;
    }
    if (sender.empty() || text.empty()) {
        return;
    }
    if (sender.size() > kMaxUsernameBytes || text.size() > kMaxChatMessageBytes) {
        return;
    }
    on_message_(sender, text);
}

}  // namespace chat
