#include "chat/app/receive_multiplex_loop.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/services/discovery_service.hpp"
#include "chat/services/direct_message_service.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/services/guaranteed_open_room_service.hpp"
#include "chat/services/lobby_service.hpp"
#include "chat/services/secure_room_service.hpp"
#include "chat/protocol/secure_invite.hpp"

#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <array>
#include <cstdint>
#include <exception>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>

namespace chat {

static std::string endpoint_string(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN]{};
    const char* ok = ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    const std::uint16_t port = ntohs(a.sin_port);
    if (ok == nullptr) {
        return std::string{"<invalid-ip>:"} + std::to_string(port);
    }
    return std::string{ip} + ":" + std::to_string(port);
}

void run_receive_multiplex_loop(std::atomic<bool>& running, UdpSocket& udp_sock,
                                DiscoveryService& discovery, LobbyService& lobby,
                                GroupRoomCoordinator& groups, DirectMessageService& direct,
                                GuaranteedOpenRoomService* guaranteed_open_room, SecureRoomService* secure_room,
                                std::chrono::milliseconds poll_timeout) {
    // kMaxPacketBytes+1: n > kMaxPacketBytes betyr for stort datagram.
    std::array<std::byte, kMaxPacketBytes + 1> buffer{};

    while (running.load()) {
        std::vector<pollfd> pfds;
        pfds.push_back(pollfd{udp_sock.fd(), POLLIN, 0});

        const int mfd = groups.multicast_fd();
        if (mfd >= 0 && mfd != udp_sock.fd()) {
            pfds.push_back(pollfd{mfd, POLLIN, 0});
        }

        std::size_t n_udp_mcast = pfds.size();
        const std::size_t tcp_base = pfds.size();
        if (guaranteed_open_room != nullptr) {
            guaranteed_open_room->append_poll_entries(pfds);
        }
        const std::size_t n_guaranteed_tcp = pfds.size() - tcp_base;
        if (secure_room != nullptr) {
            secure_room->append_poll_entries(pfds);
        }
        const std::size_t n_secure_tcp = pfds.size() - tcp_base - n_guaranteed_tcp;

        const int timeout_ms = static_cast<int>(poll_timeout.count());
        const int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            Logger::instance().network_recoverable("poll() in receive loop", errno);
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
            continue;
        }

        auto handle_datagram = [&](std::string_view recv_label, sockaddr_in& from, std::size_t n) {
            if (n == 0) {
                return;
            }
            if (n > kMaxPacketBytes) {
                Logger::instance().dropped_malformed_datagram("SECP line exceeds kMaxPacketBytes (possible truncation)",
                                                              n);
                return;
            }
            Logger::instance().info(std::string{recv_label} + " from=" + endpoint_string(from) +
                                    " bytes=" + std::to_string(n));
            const std::string_view line{reinterpret_cast<const char*>(buffer.data()), n};
            auto msg = parse_secp_line(line);
            if (!msg) {
                Logger::instance().dropped_malformed_datagram("not valid SECP (ignored)", n);
                return;
            }

            try {
                switch (msg->type) {
                    case SecpType::Presence:
                        discovery.on_presence(msg->username, msg->payload, from);
                        discovery.prune_stale_peers();
                        break;
                    case SecpType::RoomAnnounce:
                        groups.on_room_announce(*msg, from);
                        if (guaranteed_open_room != nullptr) {
                            guaranteed_open_room->on_room_announce(*msg, from);
                        }
                        break;
                    case SecpType::Invite:
                        if (is_secure_tcp_invite_payload(msg->payload)) {
                            if (secure_room != nullptr) {
                                secure_room->on_udp_invite(*msg, from);
                            }
                        } else {
                            direct.on_invite(*msg, from);
                        }
                        break;
                    case SecpType::Chat:
                        if (msg->room == "USN Chat") {
                            lobby.on_lobby_chat(msg->username, msg->payload);
                        }
                        groups.on_room_chat(*msg, from);
                        direct.on_room_chat(*msg, from);
                        break;
                    default:
                        break;
                }
            } catch (const std::exception& ex) {
                Logger::instance().error(std::string{"exception in UDP handler (dropped): "} + ex.what());
            } catch (...) {
                Logger::instance().error("unknown exception in UDP handler (dropped)");
            }
        };

        for (std::size_t i = 0; i < n_udp_mcast && i < pfds.size(); ++i) {
            auto& pfd = pfds[i];
            if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) {
                if (!running.load()) {
                    break;
                }
                continue;
            }
            if ((pfd.revents & POLLIN) == 0) {
                continue;
            }

            sockaddr_in from{};
            std::size_t n = 0;
            if (pfd.fd == udp_sock.fd()) {
                if (!udp_sock.recv_from(buffer, from, n)) {
                    if (udp_sock.last_errno() != 0) {
                        Logger::instance().socket_error("recv udp", udp_sock.last_errno());
                    }
                    continue;
                }
                handle_datagram("recv udp", from, n);
            } else if (pfd.fd == mfd) {
                if (!groups.recv_multicast(buffer, from, n)) {
                    continue;
                }
                handle_datagram("recv multicast", from, n);
            }
        }

        try {
            if (guaranteed_open_room != nullptr && n_guaranteed_tcp > 0) {
                guaranteed_open_room->process_tcp_poll_events(pfds, tcp_base, n_guaranteed_tcp);
            }
        } catch (const std::exception& ex) {
            Logger::instance().error(std::string{"exception in guaranteed TCP poll: "} + ex.what());
        } catch (...) {
            Logger::instance().error("unknown exception in guaranteed TCP poll");
        }

        try {
            if (secure_room != nullptr && n_secure_tcp > 0) {
                secure_room->process_tcp_poll_events(pfds, tcp_base + n_guaranteed_tcp, n_secure_tcp);
            }
        } catch (const std::exception& ex) {
            Logger::instance().error(std::string{"exception in secure TCP poll: "} + ex.what());
        } catch (...) {
            Logger::instance().error("unknown exception in secure TCP poll");
        }
    }
}

}  // namespace chat
