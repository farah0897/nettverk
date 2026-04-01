#include "chat/services/secure_room_service.hpp"

// Krypto: seal/open og random; ChaCha/HMAC ligger i crypto/.
#include "chat/crypto/random_bytes.hpp"
#include "chat/crypto/secure_message_crypto.hpp"
#include "chat/log/logger.hpp"
#include "chat/net/tcp_client.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/secure_invite.hpp"
#include "chat/state/user_directory.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/util/string_trim.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <string_view>

namespace chat {

namespace {

constexpr std::size_t kMaxTcpFrame = 4096;

std::string to_hex(std::span<const std::uint8_t, 32> key) {
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        out[i * 2] = hx[key[i] >> 4];
        out[i * 2 + 1] = hx[key[i] & 0xf];
    }
    return out;
}

bool from_hex(std::string_view hex, std::array<std::uint8_t, 32>& out) {
    if (hex.size() != 64) {
        return false;
    }
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = hex_val(hex[i * 2]);
        const int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool parse_secure_invite_payload(std::string_view payload, std::string_view& to_out, std::string& sess_out,
                                 std::uint16_t& port_out, std::array<std::uint8_t, 32>& key_out) {
    // CLOSED;to=X;secure_tcp=1;port=P;sess=S;key=HEX64
    bool key_ok = false;
    if (payload.rfind("CLOSED;to=", 0) != 0) {
        return false;
    }
    std::string_view rest = payload.substr(std::string_view{"CLOSED;to="}.size());
    const auto semi = rest.find(';');
    if (semi == std::string_view::npos) {
        return false;
    }
    to_out = rest.substr(0, semi);
    rest = rest.substr(semi + 1);
    if (rest.find("secure_tcp=1") == std::string_view::npos) {
        return false;
    }
    port_out = kTcpSecurePort;
    sess_out.clear();
    std::string_view p = rest;
    while (!p.empty()) {
        const auto n = p.find(';');
        std::string_view tok = (n == std::string_view::npos) ? p : p.substr(0, n);
        if (n == std::string_view::npos) {
            p = "";
        } else {
            p = p.substr(n + 1);
        }
        if (tok.rfind("port=", 0) == 0) {
            const auto v = tok.substr(std::string_view{"port="}.size());
            unsigned long prt = 0;
            for (char c : v) {
                if (c < '0' || c > '9') {
                    return false;
                }
                prt = prt * 10UL + static_cast<unsigned long>(c - '0');
                if (prt > 65535UL) {
                    return false;
                }
            }
            port_out = static_cast<std::uint16_t>(prt);
        } else if (tok.rfind("sess=", 0) == 0) {
            sess_out = std::string{tok.substr(std::string_view{"sess="}.size())};
        } else if (tok.rfind("key=", 0) == 0) {
            const auto hx = tok.substr(std::string_view{"key="}.size());
            if (!from_hex(hx, key_out)) {
                return false;
            }
            key_ok = true;
        }
    }
    return !sess_out.empty() && key_ok;
}

std::string make_session_id() {
    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<unsigned> d(0, 0xffffffU);
    char buf[7];
    std::snprintf(buf, sizeof(buf), "%06x", d(gen));
    return std::string{buf};
}

}  // namespace

SecureRoomService::SecureRoomService(UdpSocket& udp, UserDirectory& users, std::string self_username)
    : udp_{&udp}, users_{&users}, username_{std::move(self_username)} {}

SecureRoomPhase SecureRoomService::phase() const {
    std::lock_guard lock{mutex_};
    return phase_;
}

void SecureRoomService::set_on_secure_message(SecureMessageHandler h) {
    std::lock_guard lock{mutex_};
    on_msg_ = std::move(h);
}

bool SecureRoomService::send_secure_invite(const std::string& target_username) {
    std::string target = target_username;
    trim_in_place(target);
    if (target.empty() || target.size() > kMaxUsernameBytes || target == username_) {
        return false;
    }
    const auto addr = users_->find_address(target);
    if (!addr) {
        return false;
    }

    std::array<std::uint8_t, 32> key{};
    if (!crypto::fill_random(std::span<std::uint8_t>(key.data(), key.size()))) {
        return false;
    }

    const std::string sid = make_session_id();
    const std::string hexk = to_hex(key);
    const std::string payload = std::string{"CLOSED;to="} + target + ";secure_tcp=1;port=" +
                                std::to_string(kTcpSecurePort) + ";sess=" + sid + ";key=" + hexk;
    const std::string line = build_secp_line(SecpType::Invite, sid, username_, payload);
    if (line.empty()) {
        return false;
    }

    std::lock_guard lock{mutex_};
    leave_unlocked();

    invited_username_ = target;
    session_id_ = sid;
    psk_ = key;
    role_ = Role::InviterHost;

    if (!server_.listen_on(kTcpSecurePort)) {
        role_ = Role::None;
        phase_ = SecureRoomPhase::Idle;
        session_id_.clear();
        invited_username_.clear();
        psk_.fill(0);
        return false;
    }
    if (!server_.set_non_blocking(true)) {
        server_.close();
        role_ = Role::None;
        phase_ = SecureRoomPhase::Idle;
        session_id_.clear();
        invited_username_.clear();
        psk_.fill(0);
        return false;
    }

    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()), line.size()};
    sockaddr_in dest = *addr;
    dest.sin_port = htons(kUdpPort);
    if (!udp_->send_to(bytes, dest)) {
        server_.close();
        role_ = Role::None;
        phase_ = SecureRoomPhase::Idle;
        session_id_.clear();
        invited_username_.clear();
        psk_.fill(0);
        return false;
    }
    phase_ = SecureRoomPhase::HostListening;
    return true;
}

void SecureRoomService::on_udp_invite(const SecpMessage& msg, const sockaddr_in& from) {
    if (msg.type != SecpType::Invite) {
        return;
    }
    if (!is_secure_tcp_invite_payload(msg.payload)) {
        return;
    }
    std::string_view to_user;
    std::string sess;
    std::uint16_t port = kTcpSecurePort;
    std::array<std::uint8_t, 32> key{};
    if (!parse_secure_invite_payload(msg.payload, to_user, sess, port, key)) {
        return;
    }
    if (to_user != username_) {
        return;
    }
    // Koble SECP-linjen til payload: ROOM må være samme token som sess= (rom-/sesjons-id).
    if (msg.room != sess) {
        Logger::instance().info("secure invite rejected: SECP ROOM does not match sess= in payload");
        return;
    }
    if (msg.room.empty() || msg.username.empty()) {
        return;
    }
    const auto inviter_reg = users_->find_address(msg.username);
    if (!inviter_reg || inviter_reg->sin_addr.s_addr != from.sin_addr.s_addr) {
        Logger::instance().info(
            "secure invite rejected: UDP source IP does not match UserDirectory for inviter");
        return;
    }

    std::lock_guard lock{mutex_};
    if (phase_ == SecureRoomPhase::HostListening || phase_ == SecureRoomPhase::HostHandshakePending ||
        phase_ == SecureRoomPhase::HostActive) {
        return;
    }
    if (phase_ == SecureRoomPhase::InviteeActive) {
        return;
    }

    PendingIn p;
    p.session_id = std::move(sess);
    p.owner_username = msg.username;
    p.psk = key;
    p.inviter_tcp = from;
    p.inviter_tcp.sin_port = htons(port);
    p.received = std::chrono::steady_clock::now();
    Logger::instance().info(std::string{"secure TCP invite: session="} + p.session_id + " from=\"" +
                            p.owner_username + "\"");
    pending_in_ = std::move(p);
    phase_ = SecureRoomPhase::InviteePending;
}

bool SecureRoomService::accept_secure_invite(const std::string& session_id) {
    std::string sid = session_id;
    trim_in_place(sid);
    PendingIn pending;
    {
        std::lock_guard lock{mutex_};
        if (role_ == Role::InviterHost) {
            return false;
        }
        if (phase_ != SecureRoomPhase::InviteePending || !pending_in_ || pending_in_->session_id != sid) {
            return false;
        }
        pending = *pending_in_;
    }

    auto conn = TcpClient::connect(pending.inviter_tcp);
    if (!conn) {
        return false;
    }
    if (!conn->set_non_blocking(true)) {
        return false;
    }

    // SECP inne i ciphertext: CHAT|ROOM|USER|PAYLOAD; ROOM = session-id som i INVITE.
    const std::string inner = build_secp_line(SecpType::Chat, sid, username_, kSecureTcpHandshakePayload);
    if (inner.empty()) {
        return false;
    }
    std::vector<std::uint8_t> plain(inner.begin(), inner.end());
    std::vector<std::uint8_t> sealed;
    if (!crypto::seal_message(std::span<const std::uint8_t, 32>{pending.psk}, plain, sealed)) {
        return false;
    }
    if (sealed.size() > kMaxTcpFrame) {
        return false;
    }
    std::vector<std::uint8_t> wire;
    wire.reserve(2 + sealed.size());
    wire.push_back(static_cast<std::uint8_t>((sealed.size() >> 8) & 0xff));
    wire.push_back(static_cast<std::uint8_t>(sealed.size() & 0xff));
    wire.insert(wire.end(), sealed.begin(), sealed.end());

    std::span<const std::byte> sp{reinterpret_cast<const std::byte*>(wire.data()), wire.size()};
    if (!conn->send_all(sp)) {
        return false;
    }

    std::lock_guard lock{mutex_};
    leave_unlocked();
    peer_conn_ = std::make_unique<TcpConnection>(std::move(*conn));
    psk_ = pending.psk;
    session_id_ = pending.session_id;
    owner_username_ = pending.owner_username;
    pending_in_.reset();
    role_ = Role::InviteeClient;
    phase_ = SecureRoomPhase::InviteeActive;
    inbound_accum_.clear();
    return true;
}

void SecureRoomService::decline_secure_invite(const std::string& session_id) {
    std::string sid = session_id;
    trim_in_place(sid);
    std::lock_guard lock{mutex_};
    if (pending_in_ && pending_in_->session_id == sid) {
        pending_in_.reset();
        if (phase_ == SecureRoomPhase::InviteePending) {
            phase_ = SecureRoomPhase::Idle;
        }
    }
}

bool SecureRoomService::send_secure_chat(const std::string& text) {
    std::string t = text;
    trim_in_place(t);
    if (t.empty() || t.size() > kMaxChatMessageBytes) {
        return false;
    }
    const std::string inner = build_secp_line(SecpType::Chat, session_id_, username_, t);
    if (inner.empty()) {
        return false;
    }
    std::vector<std::uint8_t> plain(inner.begin(), inner.end());
    std::vector<std::uint8_t> sealed;
    std::unique_lock<std::mutex> lk{mutex_};
    if (phase_ != SecureRoomPhase::HostActive && phase_ != SecureRoomPhase::InviteeActive) {
        return false;
    }
    if (!peer_conn_ || !peer_conn_->is_open()) {
        return false;
    }
    if (!crypto::seal_message(std::span<const std::uint8_t, 32>{psk_}, plain, sealed)) {
        return false;
    }
    if (sealed.size() > kMaxTcpFrame) {
        return false;
    }
    std::vector<std::uint8_t> wire;
    wire.reserve(2 + sealed.size());
    wire.push_back(static_cast<std::uint8_t>((sealed.size() >> 8) & 0xff));
    wire.push_back(static_cast<std::uint8_t>(sealed.size() & 0xff));
    wire.insert(wire.end(), sealed.begin(), sealed.end());
    lk.unlock();
    return peer_conn_->send_all(std::span<const std::byte>(reinterpret_cast<const std::byte*>(wire.data()),
                                                             wire.size()));
}

void SecureRoomService::reject_unauthorized_peer_unlocked() {
    peer_conn_.reset();
    inbound_accum_.clear();
    if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostHandshakePending) {
        phase_ = SecureRoomPhase::HostListening;
    }
}

void SecureRoomService::leave_unlocked() {
    peer_conn_.reset();
    server_.close();
    pending_in_.reset();
    inbound_accum_.clear();
    phase_ = SecureRoomPhase::Idle;
    role_ = Role::None;
    session_id_.clear();
    invited_username_.clear();
    owner_username_.clear();
    psk_.fill(0);
}

void SecureRoomService::leave_secure_session() {
    std::lock_guard<std::mutex> lock{mutex_};
    leave_unlocked();
}

bool SecureRoomService::has_active_secure_session() const {
    std::lock_guard lock{mutex_};
    return (phase_ == SecureRoomPhase::HostActive || phase_ == SecureRoomPhase::InviteeActive) &&
           peer_conn_ && peer_conn_->is_open();
}

bool SecureRoomService::has_pending_secure_invite() const {
    std::lock_guard lock{mutex_};
    return phase_ == SecureRoomPhase::InviteePending && pending_in_.has_value();
}

std::optional<std::string> SecureRoomService::active_session_id() const {
    std::lock_guard lock{mutex_};
    if (!session_id_.empty() &&
        (phase_ == SecureRoomPhase::HostActive || phase_ == SecureRoomPhase::InviteeActive)) {
        return session_id_;
    }
    return std::nullopt;
}

std::vector<SecureRoomService::PendingSecureInvite> SecureRoomService::list_pending_secure_invites() const {
    std::lock_guard lock{mutex_};
    std::vector<PendingSecureInvite> out;
    if (phase_ == SecureRoomPhase::InviteePending && pending_in_) {
        PendingSecureInvite p;
        p.session_id = pending_in_->session_id;
        p.from_username = pending_in_->owner_username;
        p.room_label = pending_in_->session_id;
        out.push_back(std::move(p));
    }
    return out;
}

void SecureRoomService::append_poll_entries(std::vector<pollfd>& out) const {
    std::lock_guard lock{mutex_};
    if (role_ == Role::InviterHost && server_.is_open() &&
        (phase_ == SecureRoomPhase::HostListening || phase_ == SecureRoomPhase::HostHandshakePending ||
         phase_ == SecureRoomPhase::HostActive)) {
        out.push_back(pollfd{server_.fd(), POLLIN, 0});
    }
    if (peer_conn_ && peer_conn_->is_open()) {
        out.push_back(pollfd{peer_conn_->fd(), POLLIN, 0});
    }
}

bool SecureRoomService::read_frame_unlocked(TcpConnection& conn, std::vector<std::uint8_t>& out,
                                            bool& peer_closed) {
    peer_closed = false;
    std::array<std::byte, 2048> chunk{};
    std::size_t n = 0;
    if (!conn.recv_some(chunk, n)) {
        return false;
    }
    if (n == 0) {
        peer_closed = true;
        inbound_accum_.clear();
        return false;
    }
    inbound_accum_.insert(inbound_accum_.end(), reinterpret_cast<const std::uint8_t*>(chunk.data()),
                          reinterpret_cast<const std::uint8_t*>(chunk.data()) + n);
    if (inbound_accum_.size() < 2) {
        return false;
    }
    const std::size_t flen = (static_cast<std::size_t>(inbound_accum_[0]) << 8) | inbound_accum_[1];
    if (flen == 0 || flen > kMaxTcpFrame) {
        inbound_accum_.clear();
        return false;
    }
    if (inbound_accum_.size() < 2 + flen) {
        return false;
    }
    out.assign(inbound_accum_.begin() + 2, inbound_accum_.begin() + 2 + static_cast<std::ptrdiff_t>(flen));
    inbound_accum_.erase(inbound_accum_.begin(), inbound_accum_.begin() + 2 + static_cast<std::ptrdiff_t>(flen));
    return true;
}

void SecureRoomService::process_tcp_poll_events(const std::vector<pollfd>& pfds, std::size_t base_index,
                                                std::size_t count) {
    std::unique_lock<std::mutex> lk{mutex_};
    for (std::size_t i = 0; i < count; ++i) {
        const pollfd& p = pfds[base_index + i];
        if (p.fd < 0) {
            continue;
        }
        if ((p.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            leave_unlocked();
            continue;
        }
        if ((p.revents & POLLIN) == 0) {
            continue;
        }
        if (role_ == Role::InviterHost && server_.is_open() && p.fd == server_.fd()) {
            auto acc = server_.accept_peer(nullptr);
            if (acc) {
                acc->set_non_blocking(true);
                if (peer_conn_) {
                    acc->close();
                } else {
                    peer_conn_ = std::make_unique<TcpConnection>(std::move(*acc));
                    inbound_accum_.clear();
                    phase_ = SecureRoomPhase::HostHandshakePending;
                }
            }
            continue;
        }
        if (!peer_conn_ || p.fd != peer_conn_->fd()) {
            continue;
        }
        while (true) {
            std::vector<std::uint8_t> frame;
            bool peer_closed = false;
            if (!read_frame_unlocked(*peer_conn_, frame, peer_closed)) {
                if (peer_closed) {
                    if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostHandshakePending) {
                        reject_unauthorized_peer_unlocked();
                    } else {
                        leave_unlocked();
                    }
                }
                break;
            }

            std::vector<std::uint8_t> plain;
            if (!crypto::open_message(std::span<const std::uint8_t, 32>{psk_},
                                      std::span<const std::uint8_t>(frame.data(), frame.size()), plain)) {
                if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostHandshakePending) {
                    reject_unauthorized_peer_unlocked();
                } else if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostActive) {
                    leave_unlocked();
                } else if (role_ == Role::InviteeClient && phase_ == SecureRoomPhase::InviteeActive) {
                    leave_unlocked();
                }
                break;
            }

            const std::string msg(plain.begin(), plain.end());

            if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostHandshakePending) {
                const auto parsed = parse_secp_line(msg);
                if (!parsed || parsed->type != SecpType::Chat) {
                    reject_unauthorized_peer_unlocked();
                    break;
                }
                if (parsed->room != session_id_ || parsed->username != invited_username_ ||
                    parsed->payload != kSecureTcpHandshakePayload) {
                    reject_unauthorized_peer_unlocked();
                    break;
                }
                sockaddr_in peer{};
                socklen_t pl = sizeof(peer);
                if (::getpeername(peer_conn_->fd(), reinterpret_cast<sockaddr*>(&peer), &pl) != 0 ||
                    peer.sin_family != AF_INET) {
                    reject_unauthorized_peer_unlocked();
                    break;
                }
                const auto exp = users_->find_address(invited_username_);
                if (!exp || exp->sin_addr.s_addr != peer.sin_addr.s_addr) {
                    reject_unauthorized_peer_unlocked();
                    break;
                }
                phase_ = SecureRoomPhase::HostActive;
                continue;
            }

            if (role_ == Role::InviterHost && phase_ == SecureRoomPhase::HostActive) {
                const auto parsed = parse_secp_line(msg);
                if (!parsed) {
                    continue;
                }
                if (parsed->type != SecpType::Chat) {
                    continue;
                }
                if (parsed->room != session_id_) {
                    continue;
                }
                if (parsed->username.empty() || parsed->payload.empty()) {
                    continue;
                }
                if (parsed->payload == kSecureTcpHandshakePayload) {
                    continue;
                }
                SecureMessageHandler cb = on_msg_;
                const std::string sid = session_id_;
                const std::string sender = parsed->username;
                const std::string body = parsed->payload;
                lk.unlock();
                if (cb) {
                    cb(sid, sender, body);
                }
                lk.lock();
                continue;
            }

            if (role_ == Role::InviteeClient && phase_ == SecureRoomPhase::InviteeActive) {
                const auto parsed = parse_secp_line(msg);
                if (!parsed) {
                    continue;
                }
                if (parsed->type != SecpType::Chat) {
                    continue;
                }
                if (parsed->room != session_id_) {
                    continue;
                }
                if (parsed->username.empty() || parsed->payload.empty()) {
                    continue;
                }
                if (parsed->payload == kSecureTcpHandshakePayload) {
                    continue;
                }
                SecureMessageHandler cb = on_msg_;
                const std::string sid = session_id_;
                const std::string sender = parsed->username;
                const std::string body = parsed->payload;
                lk.unlock();
                if (cb) {
                    cb(sid, sender, body);
                }
                lk.lock();
                continue;
            }

            break;
        }
    }
}

}  // namespace chat
