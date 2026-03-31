#pragma once

#include <atomic>
#include <chrono>

namespace chat {

class UdpSocket;
class DiscoveryService;
class LobbyService;
class GroupRoomCoordinator;
class DirectMessageService;

/// RFC/SECP: all UDP traffic on one socket/port. Poll and dispatch by message type.
void run_receive_multiplex_loop(std::atomic<bool>& running, UdpSocket& udp_sock,
                                DiscoveryService& discovery, LobbyService& lobby,
                                GroupRoomCoordinator& groups, DirectMessageService& direct,
                                std::chrono::milliseconds poll_timeout);

}  // namespace chat
