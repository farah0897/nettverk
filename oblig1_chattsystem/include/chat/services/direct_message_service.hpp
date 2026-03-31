#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>

namespace chat {

class UdpSocket;
class UserDirectory;
struct SecpMessage;

enum class DirectSessionState : std::uint8_t {
    OutgoingPending,
    IncomingPending,
    Active,
};

struct PendingInviteInfo {
    std::string session_id;
    std::string from_username;
    std::string to_username;
};

struct ActiveDirectSessionInfo {
    std::string session_id;
    std::string peer_username;
    std::string peer_ip;
};

/// 1-1: invitasjon/accept via unicast, meldinger via unicast.
class DirectMessageService {
public:
    DirectMessageService(UdpSocket& socket, UserDirectory& users, std::string self_username);

    void set_on_private_message(std::function<void(const std::string& session_id,
                                                   const std::string& sender,
                                                   const std::string& text)> cb);
    void set_on_invite(std::function<void(const std::string& session_id,
                                          const std::string& from_username)> cb);
    void set_on_status(std::function<void(const std::string& msg)> cb);

    /// Sender INVITE_PRIVATE. Returnerer session_id ved suksess.
    std::optional<std::string> invite_user(const std::string& target_username);
    // RFC SECP har ikke eksplisitt ACCEPT/REJECT meldingstype for UDP-lukkede rom.
    // Vi beholder menyflyt ved å mappe accept/decline til intern sesjonstilstand.
    bool accept_invite(const std::string& room_name);
    bool decline_invite(const std::string& room_name, const std::string& reason);

    /// Sender PRIVATE_CHAT kun hvis session_id er aktiv og dest matcher etablert peer.
    bool send_private_chat(const std::string& session_id, const std::string& text);

    /// SECP handlers (decoded in receive loop).
    void on_invite(const SecpMessage& msg, const sockaddr_in& from);
    void on_room_chat(const SecpMessage& msg, const sockaddr_in& from);

    std::vector<PendingInviteInfo> snapshot_pending_invites() const;
    std::vector<ActiveDirectSessionInfo> snapshot_active_sessions() const;

    /// Rydd opp i gamle pending invitasjoner (defensivt i UDP-system).
    void prune_pending(std::chrono::seconds max_age);

    /// Republiserer utgående invitasjoner (OutgoingPending) periodisk for å tåle UDP-tap.
    /// - `min_interval`: resend tidligst så ofte
    /// - `max_age`: utløper invitasjoner eldre enn dette (fjernes)
    void resend_outgoing_pending(std::chrono::milliseconds min_interval, std::chrono::seconds max_age);

private:
    struct Session {
        DirectSessionState state{DirectSessionState::OutgoingPending};
        std::string session_id;
        std::string peer_username;
        sockaddr_in peer_addr{};
        std::chrono::steady_clock::time_point created_at{};
        std::chrono::steady_clock::time_point last_sent{};
        std::chrono::steady_clock::time_point last_update{};
    };

    static std::string sockaddr_ipv4_string(const sockaddr_in& a);
    static bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b);
    static std::string make_session_id();

    void status(const std::string& msg) const;

    UdpSocket* socket_{nullptr};
    UserDirectory* users_{nullptr};
    std::string self_username_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;

    std::function<void(const std::string&, const std::string&, const std::string&)> on_private_;
    std::function<void(const std::string&, const std::string&)> on_invite_;
    std::function<void(const std::string&)> on_status_;
};

}  // namespace chat
