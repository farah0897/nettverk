#pragma once

#include <atomic>
#include <chrono>

namespace chat {

class UdpSocket;
class DiscoveryService;
class LobbyService;
class GroupRoomCoordinator;
class DirectMessageService;
class GuaranteedOpenRoomService;
class SecureRoomService;

/// UDP 50000: én socket; multicast kan ha egen fd via `GroupRoomCoordinator`. TCP 50001/50002 via
/// `guaranteed_open_room` og `secure_room` (kan være nullptr). Ugyldig SECP droppes; TCP-feil isoleres med try/catch.
void run_receive_multiplex_loop(std::atomic<bool>& running, UdpSocket& udp_sock,
                                DiscoveryService& discovery, LobbyService& lobby,
                                GroupRoomCoordinator& groups, DirectMessageService& direct,
                                GuaranteedOpenRoomService* guaranteed_open_room,
                                SecureRoomService* secure_room, std::chrono::milliseconds poll_timeout);

}  // namespace chat
