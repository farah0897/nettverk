#pragma once

#include "chat/net/tcp_server.hpp"
#include "chat/protocol/tcp_line_reader.hpp"

#include <poll.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>

namespace chat {

class UdpSocket;
class GroupRoomCoordinator;
struct SecpMessage;

/// Garantert åpent TCP-rom (50001): ROOM_ANNOUNCE på UDP, SECP-linjer på TCP.
/// TCP leses i `run_receive_multiplex_loop`; meny kaller send/start/stop. `mutex_` beskytter tilstand.
/// `set_on_tcp_room_message` kjøres uten mutex (mindre risiko for deadlock ved logging).
class GuaranteedOpenRoomService {
public:
    GuaranteedOpenRoomService(UdpSocket& udp_broadcast_socket, GroupRoomCoordinator& multicast_groups,
                              std::string self_username);

    using TcpRoomMessageHandler =
        std::function<void(const std::string& room_name, const std::string& sender, const std::string& text)>;

    void set_on_tcp_room_message(TcpRoomMessageHandler h);

    [[nodiscard]] bool start_hosting(std::string room_name);

    void stop_hosting();

    [[nodiscard]] bool join_as_client(const std::string& room_name);

    void leave_tcp_room();

    [[nodiscard]] bool send_chat(const std::string& text);

    [[nodiscard]] bool is_hosting() const;
    [[nodiscard]] bool is_tcp_client() const;
    [[nodiscard]] bool has_active_tcp_room() const;
    [[nodiscard]] std::optional<std::string> active_room_name() const;

    void broadcast_owned_advert();

    void on_room_announce(const SecpMessage& msg, const sockaddr_in& from);

    struct AdvertisedTcpRoom {
        std::string room_name;
        std::string owner_username;
        std::string owner_ipv4;
        std::uint16_t tcp_port{0};
        std::chrono::steady_clock::time_point last_advert{};
    };

    [[nodiscard]] std::vector<AdvertisedTcpRoom> list_advertised_tcp_rooms() const;

    void append_poll_entries(std::vector<struct pollfd>& out) const;

    void process_tcp_poll_events(const std::vector<struct pollfd>& pfds, std::size_t base_index,
                                 std::size_t count);

private:
    struct HostedClient {
        TcpConnection conn;
        TcpLineReader reader;
        explicit HostedClient(TcpConnection&& c) : conn(std::move(c)), reader(conn) {}
    };

    /// Data til UI-callback; utleveres utenfor mutex for å unngå deadlock.
    struct PendingNotification {
        std::string room;
        std::string sender;
        std::string text;
    };

    /// Krever at `mutex_` er låst (kalles fra `start_hosting` med lås).
    void broadcast_owned_advert_unlocked();

    void prune_registry_unlocked();
    /// Krever `mutex_`. Fjerner klient ved sendefeil.
    void relay_line_to_others_unlocked(int sender_fd, const std::string& line_with_newline);
    void remove_client_by_fd_unlocked(int fd);
    /// Krever `mutex_`.
    [[nodiscard]] std::optional<PendingNotification> handle_incoming_client_line_unlocked(
        HostedClient& hc, const std::string& line);
    void accept_new_clients_unlocked();
    void dispatch_client_fd_unlocked(int fd, std::unique_lock<std::mutex>& lk);
    void process_client_role_incoming_unlocked(std::unique_lock<std::mutex>& lk);

    UdpSocket* udp_{nullptr};
    GroupRoomCoordinator* groups_{nullptr};
    std::string username_;

    mutable std::mutex mutex_;
    TcpRoomMessageHandler on_msg_;

    TcpServer server_;
    std::vector<std::unique_ptr<HostedClient>> clients_;
    std::string hosting_room_name_;
    bool hosting_{false};

    std::unique_ptr<TcpConnection> client_conn_;
    std::unique_ptr<TcpLineReader> client_reader_;
    std::string client_room_name_;
    bool client_role_{false};

    std::unordered_map<std::string, AdvertisedTcpRoom> tcp_registry_;
};

}  // namespace chat
