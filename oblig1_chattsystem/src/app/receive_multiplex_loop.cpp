#include "chat/app/receive_multiplex_loop.hpp"

#include "chat/log/logger.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/secp.hpp"
#include "chat/services/discovery_service.hpp"
#include "chat/services/direct_message_service.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/services/lobby_service.hpp"

#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <array>
#include <cstdint>
#include <poll.h>
#include <string>
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
                                std::chrono::milliseconds poll_timeout) {
    std::array<std::byte, kMaxPacketBytes> buffer{};

    while (running.load()) {
        std::vector<pollfd> pfds;
        pfds.push_back(pollfd{udp_sock.fd(), POLLIN, 0});

        const int mfd = groups.multicast_fd();
        if (mfd >= 0 && mfd != udp_sock.fd()) {
            pfds.push_back(pollfd{mfd, POLLIN, 0});
        }

        const int timeout_ms = static_cast<int>(poll_timeout.count());
        const int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            Logger::instance().socket_error("poll()", errno);
            break;
        }

        auto handle_datagram = [&](std::string_view recv_label, sockaddr_in& from, std::size_t n) {
            if (n == 0 || n > buffer.size()) {
                return;
            }
            Logger::instance().info(std::string{recv_label} + " from=" + endpoint_string(from) +
                                    " bytes=" + std::to_string(n));
            const std::string_view line{reinterpret_cast<const char*>(buffer.data()), n};
            auto msg = parse_secp_line(line);
            if (!msg) {
                Logger::instance().parsing_error("parse SECP line");
                return;
            }

            switch (msg->type) {
                case SecpType::Presence:
                    discovery.on_presence(msg->username, msg->payload, from);
                    discovery.prune_stale_peers();
                    break;
                case SecpType::RoomAnnounce:
                    groups.on_room_announce(*msg, from);
                    break;
                case SecpType::Invite:
                    direct.on_invite(*msg, from);
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
        };

        for (std::size_t i = 0; i < pfds.size(); ++i) {
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
                    // recv_multicast logs via socket wrapper if needed elsewhere; ignore empty.
                    continue;
                }
                handle_datagram("recv multicast", from, n);
            }
        }
    }
}

}  // namespace chat
