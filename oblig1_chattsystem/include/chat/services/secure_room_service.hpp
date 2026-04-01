#pragma once

#include "chat/net/tcp_connection.hpp"
#include "chat/net/tcp_server.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <vector>

#include <netinet/in.h>

namespace chat {

class UdpSocket;
class UserDirectory;
struct SecpMessage;

/// Livssyklus for sikker rom (én invitasjon / én sesjon om gangen).
enum class SecureRoomPhase {
    Idle,                 ///< Ingen aktivitet.
    InviteePending,       ///< UDP INVITE mottatt og lagret; bruker har ikke akseptert ennå.
    HostListening,        ///< Vert har sendt INVITE, TCP-server lytter; ingen gyldig klient ennå.
    HostHandshakePending, ///< TCP tilkoblet; første gyldige RFC SECP `CHAT|…|…|…` (håndtrykk) mangler.
    HostActive,           ///< Vert har godkjent invitert part; rommet er «åpent» for chat.
    InviteeActive,        ///< Invitert har koblet til og kan chatte.
};

/// Sikker TCP-chat (50002): UDP INVITE med PSK, deretter ChaCha20+HMAC på TCP (`seal_message` /
/// `open_message`). SECP ligger inni ciphertext. PSK i INVITE sendes i klartekst på UDP (øving).
/// Tilgang: `ROOM` matcher `sess=`; inviter- og invitert-IP sjekkes mot `UserDirectory`. Se
/// `GuaranteedOpenRoomService` (50001), multicast, `DirectMessageService`.
class SecureRoomService {
public:
    SecureRoomService(UdpSocket& udp, UserDirectory& users, std::string self_username);

    [[nodiscard]] SecureRoomPhase phase() const;

    using SecureMessageHandler =
        std::function<void(const std::string& session_id, const std::string& sender, const std::string& text)>;

    void set_on_secure_message(SecureMessageHandler h);

    /// Sender INVITE med `secure_tcp=1` + nøkkel (hex), starter TCP-lytter på `kTcpSecurePort`.
    [[nodiscard]] bool send_secure_invite(const std::string& target_username);

    void on_udp_invite(const SecpMessage& msg, const sockaddr_in& from);

    [[nodiscard]] bool accept_secure_invite(const std::string& session_id);
    void decline_secure_invite(const std::string& session_id);

    [[nodiscard]] bool send_secure_chat(const std::string& text);

    void leave_secure_session();

    [[nodiscard]] bool has_active_secure_session() const;
    [[nodiscard]] bool has_pending_secure_invite() const;
    [[nodiscard]] std::optional<std::string> active_session_id() const;

    struct PendingSecureInvite {
        std::string session_id;
        std::string from_username;
        std::string room_label;
    };
    [[nodiscard]] std::vector<PendingSecureInvite> list_pending_secure_invites() const;

    void append_poll_entries(std::vector<pollfd>& out) const;
    void process_tcp_poll_events(const std::vector<pollfd>& pfds, std::size_t base_index, std::size_t count);

private:
    enum class Role { None, InviterHost, InviteeClient };

    void leave_unlocked();
    /// Lukker kun innkommende TCP etter mislykket håndtrykk; vert fortsetter å lytte (kun vert).
    void reject_unauthorized_peer_unlocked();
    /// `peer_closed` settes når `recv` returnerer 0 (motpart lukket).
    [[nodiscard]] bool read_frame_unlocked(TcpConnection& conn, std::vector<std::uint8_t>& out,
                                           bool& peer_closed);

    UdpSocket* udp_{nullptr};
    UserDirectory* users_{nullptr};
    std::string username_;

    mutable std::mutex mutex_;
    SecureMessageHandler on_msg_;
    Role role_{Role::None};
    SecureRoomPhase phase_{SecureRoomPhase::Idle};

    TcpServer server_;
    std::unique_ptr<TcpConnection> peer_conn_;
    std::string session_id_;
    std::array<std::uint8_t, 32> psk_{};
    std::string invited_username_;
    std::string owner_username_;

    struct PendingIn {
        std::string session_id;       ///< Må matche SECP `ROOM` og `sess=` i payload.
        std::string owner_username;
        std::array<std::uint8_t, 32> psk{};
        sockaddr_in inviter_tcp{};
        std::chrono::steady_clock::time_point received{};
    };
    std::optional<PendingIn> pending_in_;

    std::vector<std::uint8_t> inbound_accum_;
};

}  // namespace chat
