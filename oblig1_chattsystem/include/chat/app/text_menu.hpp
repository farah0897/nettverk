#pragma once

#include <string>

namespace chat {

class UserDirectory;
class LobbyService;
class GroupRoomCoordinator;
class DirectMessageService;
class GuaranteedOpenRoomService;
class SecureRoomService;

/// Kommandoløkke (stdin). Nettverkstråder kjører uavhengig; ikke lås rundt blokkerende lesing.
/// All utskrift til konsoll går via `cout_mutex` der det trengs.
/// @return true ved avslutt (siste menyvalg), false ved EOF på stdin.
bool run_text_menu_loop(const std::string& username, UserDirectory& users, LobbyService& lobby,
                        GroupRoomCoordinator& groups, DirectMessageService& direct,
                        GuaranteedOpenRoomService& guaranteed_open, SecureRoomService& secure_room);

}  // namespace chat
