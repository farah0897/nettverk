#pragma once

#include <string>

namespace chat {

class UserDirectory;
class LobbyService;
class GroupRoomCoordinator;
class DirectMessageService;

/// Kommandoløkke (stdin). Nettverkstråder fortsetter uavhengig; aldri lås rundt blokkerende lesing.
/// @return true ved avslutt (valg 10), false ved EOF på stdin.
bool run_text_menu_loop(const std::string& username, UserDirectory& users, LobbyService& lobby,
                        GroupRoomCoordinator& groups, DirectMessageService& direct);

}  // namespace chat
