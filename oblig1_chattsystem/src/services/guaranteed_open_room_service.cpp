#include "chat/services/guaranteed_open_room_service.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/tcp_client.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/util/string_trim.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cerrno>
#include <span>
#include <utility>

namespace chat {

namespace {

inline constexpr int kMaxGuaranteedTcpClients = 64;
inline constexpr int kMaxLinesDrainedPerPoll = 64;
inline constexpr std::chrono::seconds kTcpRoomRegistryStaleAge{25};

std::optional<std::uint16_t> parse_open_tcp_payload(std::string_view p) {
    const std::string_view prefix = "OPEN;tcp=";
    if (p.size() <= prefix.size() || p.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    const std::string_view rest = p.substr(prefix.size());
    if (rest.empty()) {
        return std::nullopt;
    }
    unsigned long v = 0;
    for (char c : rest) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10UL + static_cast<unsigned long>(c - '0');
        if (v > 65535UL) {
            return std::nullopt;
        }
    }
    if (v == 0) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(v);
}

}  // namespace

GuaranteedOpenRoomService::GuaranteedOpenRoomService(UdpSocket& udp_broadcast_socket,
                                                       GroupRoomCoordinator& multicast_groups,
                                                       std::string self_username)
    : udp_{&udp_broadcast_socket}, groups_{&multicast_groups}, username_{std::move(self_username)} {}

void GuaranteedOpenRoomService::set_on_tcp_room_message(TcpRoomMessageHandler h) {
    std::lock_guard lock{mutex_};
    on_msg_ = std::move(h);
}

void GuaranteedOpenRoomService::prune_registry_unlocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = tcp_registry_.begin(); it != tcp_registry_.end();) {
        if (now - it->second.last_advert > kTcpRoomRegistryStaleAge) {
            it = tcp_registry_.erase(it);
        } else {
            ++it;
        }
    }
}

bool GuaranteedOpenRoomService::start_hosting(std::string room_name) {
    std::lock_guard lock{mutex_};
    if (room_name.empty() || room_name.size() > kMaxRoomNameBytes) {
        return false;
    }

    clients_.clear();
    if (server_.is_open()) {
        server_.close();
    }
    hosting_ = false;
    hosting_room_name_.clear();

    groups_->leave_room();

    if (!server_.listen_on(kTcpPort)) {
        Logger::instance().socket_error("TCP listen (guaranteed room)", server_.last_errno());
        return false;
    }
    if (!server_.set_non_blocking(true)) {
        Logger::instance().socket_error("TCP listen O_NONBLOCK", server_.last_errno());
        server_.close();
        return false;
    }

    hosting_room_name_ = std::move(room_name);
    hosting_ = true;

    broadcast_owned_advert_unlocked();
    Logger::instance().info("guaranteed TCP room hosting: room=" + hosting_room_name_ + " port=" +
                            std::to_string(kTcpPort));
    return true;
}

void GuaranteedOpenRoomService::stop_hosting() {
    std::lock_guard lock{mutex_};
    clients_.clear();
    server_.close();
    hosting_ = false;
    hosting_room_name_.clear();
}

void GuaranteedOpenRoomService::leave_tcp_room() {
    std::lock_guard lock{mutex_};
    client_reader_.reset();
    client_conn_.reset();
    client_room_name_.clear();
    client_role_ = false;
}

bool GuaranteedOpenRoomService::join_as_client(const std::string& room_name) {
    if (room_name.empty() || room_name.size() > kMaxRoomNameBytes) {
        return false;
    }

    AdvertisedTcpRoom info;
    {
        std::lock_guard lock{mutex_};
        prune_registry_unlocked();
        const auto it = tcp_registry_.find(room_name);
        if (it == tcp_registry_.end()) {
            return false;
        }
        info = it->second;
    }

    client_reader_.reset();
    client_conn_.reset();
    client_role_ = false;
    client_room_name_.clear();

    groups_->leave_room();

    auto conn = TcpClient::connect_ipv4(info.owner_ipv4, info.tcp_port);
    if (!conn) {
        Logger::instance().socket_error("TCP connect (guaranteed room)", TcpClient::last_errno());
        return false;
    }
    if (!conn->set_non_blocking(true)) {
        Logger::instance().socket_error("TCP client O_NONBLOCK", conn->last_errno());
        return false;
    }

    std::lock_guard lock{mutex_};
    client_conn_ = std::make_unique<TcpConnection>(std::move(*conn));
    client_reader_ = std::make_unique<TcpLineReader>(*client_conn_);
    client_room_name_ = room_name;
    client_role_ = true;
    Logger::instance().info("joined guaranteed TCP room: " + room_name + " @ " + info.owner_ipv4 +
                            ":" + std::to_string(info.tcp_port));
    return true;
}

bool GuaranteedOpenRoomService::send_chat(const std::string& text) {
    std::string trimmed = text;
    trim_in_place(trimmed);
    if (trimmed.empty() || trimmed.size() > kMaxChatMessageBytes) {
        return false;
    }

    std::string room;
    bool as_host = false;
    {
        std::lock_guard lock{mutex_};
        if (hosting_) {
            room = hosting_room_name_;
            as_host = true;
        } else if (client_role_) {
            room = client_room_name_;
            as_host = false;
        } else {
            return false;
        }
    }
    if (room.empty()) {
        return false;
    }

    const std::string line = build_secp_line(SecpType::Chat, room, username_, trimmed);
    if (line.empty()) {
        return false;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};

    std::lock_guard lock{mutex_};
    if (as_host) {
        if (!hosting_) {
            return false;
        }
        std::vector<int> dead_fds;
        bool any_ok = false;
        for (auto& c : clients_) {
            if (c->conn.send_all(bytes)) {
                any_ok = true;
            } else {
                dead_fds.push_back(c->conn.fd());
            }
        }
        for (int fd : dead_fds) {
            remove_client_by_fd_unlocked(fd);
        }
        return any_ok || clients_.empty();
    }
    if (client_role_ && client_conn_ && client_conn_->is_open()) {
        if (!client_conn_->send_all(bytes)) {
            client_reader_.reset();
            client_conn_.reset();
            client_room_name_.clear();
            client_role_ = false;
            return false;
        }
        return true;
    }
    return false;
}

bool GuaranteedOpenRoomService::is_hosting() const {
    std::lock_guard lock{mutex_};
    return hosting_;
}

bool GuaranteedOpenRoomService::is_tcp_client() const {
    std::lock_guard lock{mutex_};
    return client_role_;
}

bool GuaranteedOpenRoomService::has_active_tcp_room() const {
    std::lock_guard lock{mutex_};
    return (hosting_ && server_.is_open()) || (client_role_ && client_conn_ && client_conn_->is_open());
}

std::optional<std::string> GuaranteedOpenRoomService::active_room_name() const {
    std::lock_guard lock{mutex_};
    if (hosting_ && !hosting_room_name_.empty()) {
        return hosting_room_name_;
    }
    if (client_role_ && !client_room_name_.empty()) {
        return client_room_name_;
    }
    return std::nullopt;
}

void GuaranteedOpenRoomService::broadcast_owned_advert_unlocked() {
    if (!hosting_ || hosting_room_name_.empty() || udp_ == nullptr) {
        return;
    }
    const std::string room = hosting_room_name_;
    const std::string user = username_;

    const std::string payload = std::string{"OPEN;tcp="} + std::to_string(kTcpPort);
    const std::string line = build_secp_line(SecpType::RoomAnnounce, room, user, payload);
    if (line.empty()) {
        return;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line.data()),
                                           line.size()};
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kUdpPort);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (!udp_->send_to(bytes, dest)) {
        Logger::instance().socket_error("ROOM_ANNOUNCE TCP broadcast", udp_->last_errno());
    } else {
        Logger::instance().sent_broadcast("ROOM_ANNOUNCE(guaranteed TCP)", kUdpPort, bytes.size());
    }
}

void GuaranteedOpenRoomService::broadcast_owned_advert() {
    std::lock_guard lock{mutex_};
    if (!hosting_ || hosting_room_name_.empty() || udp_ == nullptr) {
        return;
    }
    broadcast_owned_advert_unlocked();
}

void GuaranteedOpenRoomService::on_room_announce(const SecpMessage& msg, const sockaddr_in& from) {
    if (msg.type != SecpType::RoomAnnounce) {
        return;
    }
    if (msg.room.empty() || msg.room == "-" || msg.username.empty()) {
        return;
    }
    const auto port = parse_open_tcp_payload(msg.payload);
    if (!port) {
        return;
    }
    char ipbuf[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf)) == nullptr) {
        return;
    }

    std::lock_guard lock{mutex_};
    AdvertisedTcpRoom ar;
    ar.room_name = msg.room;
    ar.owner_username = msg.username;
    ar.owner_ipv4 = ipbuf;
    ar.tcp_port = *port;
    ar.last_advert = std::chrono::steady_clock::now();
    tcp_registry_[msg.room] = std::move(ar);
    prune_registry_unlocked();
}

std::vector<GuaranteedOpenRoomService::AdvertisedTcpRoom> GuaranteedOpenRoomService::list_advertised_tcp_rooms()
    const {
    std::lock_guard lock{mutex_};
    std::vector<AdvertisedTcpRoom> out;
    out.reserve(tcp_registry_.size());
    for (const auto& [k, v] : tcp_registry_) {
        (void)k;
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(),
              [](const AdvertisedTcpRoom& a, const AdvertisedTcpRoom& b) {
                  return a.room_name < b.room_name;
              });
    return out;
}

void GuaranteedOpenRoomService::append_poll_entries(std::vector<pollfd>& out) const {
    std::lock_guard lock{mutex_};
    if (hosting_ && server_.is_open()) {
        out.push_back(pollfd{server_.fd(), POLLIN, 0});
    }
    for (const auto& c : clients_) {
        if (c->conn.is_open()) {
            out.push_back(pollfd{c->conn.fd(), POLLIN, 0});
        }
    }
    if (client_role_ && client_conn_ && client_conn_->is_open()) {
        out.push_back(pollfd{client_conn_->fd(), POLLIN, 0});
    }
}

void GuaranteedOpenRoomService::relay_line_to_others_unlocked(int sender_fd,
                                                              const std::string& line_with_newline) {
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(line_with_newline.data()),
                                           line_with_newline.size()};
    std::vector<int> dead_fds;
    for (auto& c : clients_) {
        if (c->conn.fd() == sender_fd) {
            continue;
        }
        if (!c->conn.send_all(bytes)) {
            dead_fds.push_back(c->conn.fd());
        }
    }
    for (int fd : dead_fds) {
        remove_client_by_fd_unlocked(fd);
    }
}

void GuaranteedOpenRoomService::remove_client_by_fd_unlocked(int fd) {
    const auto before = clients_.size();
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [fd](const std::unique_ptr<HostedClient>& p) {
                                      return p->conn.fd() == fd;
                                  }),
                   clients_.end());
    if (clients_.size() != before) {
        Logger::instance().info("guaranteed TCP: peer disconnected fd=" + std::to_string(fd));
    }
}

std::optional<GuaranteedOpenRoomService::PendingNotification>
GuaranteedOpenRoomService::handle_incoming_client_line_unlocked(HostedClient& hc, const std::string& line) {
    // RFC SECP: newline-delt linje uten `\n` i `line`. Ugyldig / ukjent type: ignorer uten krasj.
    auto msg = parse_secp_line(line);
    if (!msg) {
        return std::nullopt;
    }
    if (msg->type != SecpType::Chat) {
        return std::nullopt;
    }
    if (msg->room != hosting_room_name_) {
        return std::nullopt;
    }
    if (msg->username.empty() || msg->payload.empty()) {
        return std::nullopt;
    }
    if (msg->username == username_) {
        return std::nullopt;
    }
    const std::string rebuilt =
        build_secp_line(SecpType::Chat, msg->room, msg->username, msg->payload);
    if (rebuilt.empty()) {
        return std::nullopt;
    }
    relay_line_to_others_unlocked(hc.conn.fd(), rebuilt);
    return PendingNotification{msg->room, msg->username, msg->payload};
}

void GuaranteedOpenRoomService::accept_new_clients_unlocked() {
    int accepted = 0;
    while (server_.is_open() && accepted < 32) {
        auto peer = server_.accept_peer(nullptr);
        if (!peer) {
            if (server_.last_errno() == EAGAIN || server_.last_errno() == EWOULDBLOCK) {
                break;
            }
            Logger::instance().socket_error("accept (guaranteed room)", server_.last_errno());
            break;
        }
        if (static_cast<int>(clients_.size()) >= kMaxGuaranteedTcpClients) {
            peer->close();
            continue;
        }
        if (!peer->set_non_blocking(true)) {
            Logger::instance().socket_error("accepted client O_NONBLOCK", peer->last_errno());
            peer->close();
            continue;
        }
        clients_.push_back(std::make_unique<HostedClient>(std::move(*peer)));
        ++accepted;
    }
}

void GuaranteedOpenRoomService::dispatch_client_fd_unlocked(int fd, std::unique_lock<std::mutex>& lk) {
    int drained = 0;
    while (drained < kMaxLinesDrainedPerPoll) {
        HostedClient* target = nullptr;
        for (auto& c : clients_) {
            if (c->conn.fd() == fd) {
                target = c.get();
                break;
            }
        }
        if (target == nullptr) {
            return;
        }

        std::string line;
        const TcpLineReadStatus st = target->reader.try_read_line(line);
        if (st == TcpLineReadStatus::NeedMoreData) {
            return;
        }
        if (st == TcpLineReadStatus::Disconnected || st == TcpLineReadStatus::Error) {
            remove_client_by_fd_unlocked(fd);
            return;
        }

        std::optional<PendingNotification> note = handle_incoming_client_line_unlocked(*target, line);
        ++drained;

        if (note) {
            TcpRoomMessageHandler cb_copy = on_msg_;
            lk.unlock();
            if (cb_copy) {
                cb_copy(note->room, note->sender, note->text);
            }
            lk.lock();
        }

        // Klient kan være fjernet under relay (død send); finn på nytt eller avslutt.
        bool still_there = false;
        for (const auto& c : clients_) {
            if (c->conn.fd() == fd) {
                still_there = true;
                break;
            }
        }
        if (!still_there) {
            return;
        }
    }
}

void GuaranteedOpenRoomService::process_client_role_incoming_unlocked(std::unique_lock<std::mutex>& lk) {
    if (!client_reader_ || !client_conn_) {
        return;
    }
    int drained = 0;
    while (drained < kMaxLinesDrainedPerPoll) {
        std::string line;
        const TcpLineReadStatus st = client_reader_->try_read_line(line);
        if (st == TcpLineReadStatus::NeedMoreData) {
            return;
        }
        if (st == TcpLineReadStatus::Disconnected || st == TcpLineReadStatus::Error) {
            if (st == TcpLineReadStatus::Disconnected) {
                Logger::instance().info("guaranteed TCP client: server closed connection or EOF");
            } else {
                Logger::instance().warn("guaranteed TCP client: line read error, closing");
            }
            client_reader_.reset();
            client_conn_.reset();
            client_room_name_.clear();
            client_role_ = false;
            return;
        }

        auto msg = parse_secp_line(line);
        if (!msg) {
            ++drained;
            continue;
        }
        if (msg->type != SecpType::Chat) {
            ++drained;
            continue;
        }
        if (msg->room != client_room_name_) {
            ++drained;
            continue;
        }
        if (msg->username.empty() || msg->payload.empty()) {
            ++drained;
            continue;
        }
        if (msg->username == username_) {
            ++drained;
            continue;
        }

        TcpRoomMessageHandler cb_copy = on_msg_;
        const std::string room = msg->room;
        const std::string sender = msg->username;
        const std::string payload = msg->payload;
        ++drained;

        lk.unlock();
        if (cb_copy) {
            cb_copy(room, sender, payload);
        }
        lk.lock();

        if (!client_role_ || !client_conn_) {
            return;
        }
    }
}

void GuaranteedOpenRoomService::process_tcp_poll_events(const std::vector<pollfd>& pfds,
                                                        const std::size_t base_index,
                                                        const std::size_t count) {
    std::unique_lock<std::mutex> lk{mutex_};
    for (std::size_t i = 0; i < count; ++i) {
        const pollfd& p = pfds[base_index + i];
        if (p.fd < 0) {
            continue;
        }
        if ((p.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (hosting_ && server_.is_open() && p.fd == server_.fd()) {
                clients_.clear();
                server_.close();
                hosting_ = false;
                hosting_room_name_.clear();
                continue;
            }
            if (client_role_ && client_conn_ && p.fd == client_conn_->fd()) {
                client_reader_.reset();
                client_conn_.reset();
                client_room_name_.clear();
                client_role_ = false;
                continue;
            }
            remove_client_by_fd_unlocked(p.fd);
            continue;
        }
        if ((p.revents & POLLIN) == 0) {
            continue;
        }
        if (hosting_ && server_.is_open() && p.fd == server_.fd()) {
            accept_new_clients_unlocked();
        } else if (client_role_ && client_conn_ && p.fd == client_conn_->fd()) {
            process_client_role_incoming_unlocked(lk);
        } else {
            dispatch_client_fd_unlocked(p.fd, lk);
        }
    }
}

}  // namespace chat
