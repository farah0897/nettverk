#include "chat/app/text_menu.hpp"

#include "chat/app/sync_output.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/services/direct_message_service.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/services/lobby_service.hpp"
#include "chat/state/user_directory.hpp"
#include "chat/util/string_trim.hpp"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

namespace {

void print_menu() {
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n   HOVEDMENY: Velg 1-12:    \n"
              << "  1. Vis aktive brukere\n"
              << "  2. Send melding til USN Chat\n"
              << "  3. Opprett åpent grupperom\n"
              << "  4. Vis tilgjengelige grupperom\n"
              << "  5. Bli med i grupperom\n"
              << "  6. Send melding til grupperom\n"
              << "  7. Forlat grupperom\n"
              << "  8. Inviter bruker til 1-1 rom\n"
              << "  9. Svar på invitasjon (aksept/avslag)\n"
              << " 10. Send privat melding\n"
              << " 11. Vis aktive private samtaler\n"
              << " 12. Avslutt\n"
              << "--------------------------------\n"
              << std::flush;
}

void print_error(const std::string& msg) {
    std::lock_guard lock{chat::cout_mutex};
    std::cerr << msg << '\n' << std::flush;
}

void print_line(const std::string& msg) {
    std::lock_guard lock{chat::cout_mutex};
    std::cout << msg << '\n' << std::flush;
}

// Prints prompt (thread-safe), reads a line, trims in-place.
// Returns false on stdin EOF/error (caller should terminate app).
bool read_trimmed_line(const char* prompt, std::string& out) {
    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << prompt << std::flush;
    }
    out.clear();
    if (!std::getline(std::cin, out)) {
        return false;
    }
    chat::trim_in_place(out);
    return true;
}

void print_active_users(const chat::UserDirectory& dir) {
    const auto peers = dir.snapshot_peers();
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n--- Aktive brukere (" << peers.size() << ") ---\n";
    if (peers.empty()) {
        std::cout << "  (ingen andre oppdaget ennå)\n";
        return;
    }
    for (const auto& p : peers) {
        const auto ago =
            std::chrono::duration_cast<std::chrono::seconds>(now - p.last_seen).count();
        std::cout << "  " << p.username << " @ " << p.ipv4 << " — sist sett for " << ago
                  << " s siden\n";
    }
    std::cout.flush();
}

void print_advertised_rooms(chat::GroupRoomCoordinator& groups) {
    auto list = groups.list_advertised_rooms();
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n--- Tilgjengelige grupperom (" << list.size() << ") ---\n";
    if (list.empty()) {
        std::cout << "  (ingen annonser ennå — vent noen sekunder)\n";
        return;
    }
    for (const auto& r : list) {
        std::cout << "  Rom: " << r.room_name << " @ "
                  << r.multicast_ip << ":" << r.multicast_port << '\n';
    }
    std::cout.flush();
}

std::optional<int> parse_menu_choice(std::string s) {
    chat::trim_in_place(s);
    if (s.empty()) {
        return std::nullopt;
    }
    char* end_ptr = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end_ptr, 10);
    if (errno != 0 || end_ptr == s.c_str() || *end_ptr != '\0') {
        return std::nullopt;
    }
    if (v < 1 || v > 12) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

bool prompt_lobby_message(const std::string& username, chat::LobbyService& lobby) {
    std::string msg;
    if (!read_trimmed_line("Melding til USN Chat (tom linje = avbryt):\n> ", msg)) {
        return false;
    }
    if (msg.empty()) {
        print_line("(Ingen melding sendt.)");
        return true;
    }
    if (msg.size() > chat::kMaxChatMessageBytes) {
        print_error("Meldingen er for lang.");
        return true;
    }
    if (!lobby.send_chat(username, msg)) {
        print_error("Kunne ikke sende til USN Chat.");
        return true;
    }
    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[USN Chat] " << username << ": " << msg << '\n' << std::flush;
    }
    return true;
}

bool prompt_create_group(chat::GroupRoomCoordinator& groups) {
    std::string name;
    if (!read_trimmed_line("Navn på grupperom: ", name)) {
        return false;
    }
    if (name.empty()) {
        print_error("Navn kan ikke være tomt.");
        return true;
    }
    if (name.size() > chat::kMaxRoomNameBytes) {
        print_error("Navnet er for langt.");
        return true;
    }
    if (!groups.create_room(name)) {
        print_error("Kunne ikke opprette grupperom (nettverk/port opptatt?).");
        return true;
    }
    if (const auto id = groups.active_room_id()) {
        print_line("Grupperom opprettet. Romnavn: " + *id);
    }
    return true;
}

bool prompt_join_group(chat::GroupRoomCoordinator& groups) {
    std::string room_name;
    if (!read_trimmed_line("Romnavn (bruk 4 for oversikt): ", room_name)) {
        return false;
    }
    if (room_name.empty()) {
        print_error("Romnavn kan ikke være tomt.");
        return true;
    }
    if (room_name.size() > chat::kMaxRoomNameBytes) {
        print_error("Romnavn er for langt.");
        return true;
    }
    if (!groups.join_room(room_name)) {
        print_error("Kunne ikke bli med (ukjent rom, allerede medlem, eller nettverksfeil).");
        return true;
    }
    print_line("Du er nå med i rom " + room_name + ".");
    return true;
}

bool prompt_group_chat(const std::string& username, chat::GroupRoomCoordinator& groups) {
    if (!groups.has_active_membership()) {
        print_error("Du er ikke med i noe grupperom (bruk 5).");
        return true;
    }
    std::string msg;
    if (!read_trimmed_line("Melding til grupperom (tom linje = avbryt):\n> ", msg)) {
        return false;
    }
    if (msg.empty()) {
        print_line("(Ingen melding sendt.)");
        return true;
    }
    if (msg.size() > chat::kMaxChatMessageBytes) {
        print_error("Meldingen er for lang.");
        return true;
    }
    const auto room_id = groups.active_room_id();
    if (!room_id) {
        print_error("Ingen aktiv gruppe.");
        return true;
    }
    if (!groups.send_group_chat(msg)) {
        print_error("Sending til multicast feilet.");
        return true;
    }
    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[Gruppe " << *room_id << "] " << username << ": " << msg << '\n' << std::flush;
    }
    return true;
}

}  // namespace

namespace chat {

bool run_text_menu_loop(const std::string& username, UserDirectory& users, LobbyService& lobby,
                        GroupRoomCoordinator& groups, DirectMessageService& direct) {
    std::string line;
    while (true) {
        print_menu();
        if (!std::getline(std::cin, line)) {
            print_error("(stdin lukket — avslutter.)");
            return false;
        }
        chat::trim_in_place(line);
        const auto choice = parse_menu_choice(line);
        if (!choice) {
            print_error("Ugyldig valg. Skriv heltall 1–12.");
            continue;
        }
        switch (*choice) {
            case 1:
                print_active_users(users);
                break;
            case 2:
                if (!prompt_lobby_message(username, lobby)) {
                    return false;
                }
                break;
            case 3:
                if (!prompt_create_group(groups)) {
                    return false;
                }
                break;
            case 4:
                print_advertised_rooms(groups);
                break;
            case 5:
                if (!prompt_join_group(groups)) {
                    return false;
                }
                break;
            case 6:
                if (!prompt_group_chat(username, groups)) {
                    return false;
                }
                break;
            case 7:
                if (!groups.has_active_membership()) {
                    print_error("Du er ikke med i noe grupperom.");
                } else {
                    groups.leave_room();
                    print_line("Du har forlatt grupperommet.");
                }
                break;
            case 8:
            {
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "Hvem vil du invitere? (bruk 1 for å se aktive brukere)\n> "
                              << std::flush;
                }
                std::string who;
                if (!std::getline(std::cin, who)) {
                    return false;
                }
                chat::trim_in_place(who);
                if (who.empty()) {
                    print_error("Tomt brukernavn.");
                    break;
                }
                if (who.size() > chat::kMaxUsernameBytes) {
                    print_error("Brukernavn er for langt.");
                    break;
                }
                const auto sid = direct.invite_user(who);
                if (!sid) {
                    print_error("Kunne ikke sende invitasjon (ukjent bruker / nettverksfeil).");
                } else {
                    print_line("Invitasjon sendt. Session-ID: " + *sid);
                }
            }
                break;
            case 9:
            {
                const auto pending = direct.snapshot_pending_invites();
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "\n--- Pending invitasjoner (" << pending.size() << ") ---\n";
                    if (pending.empty()) {
                        std::cout << "  (ingen)\n" << std::flush;
                    } else {
                        for (const auto& p : pending) {
                            std::cout << "  Session " << p.session_id << " fra " << p.from_username
                                      << '\n';
                        }
                        std::cout << std::flush;
                    }
                }
                if (pending.empty()) {
                    break;
                }
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "Skriv session-id (tom linje = avbryt):\n> " << std::flush;
                }
                std::string sid;
                if (!std::getline(std::cin, sid)) {
                    return false;
                }
                chat::trim_in_place(sid);
                if (sid.empty()) {
                    break;
                }
                if (sid.size() > 32) {
                    print_error("Session-id er for lang.");
                    break;
                }
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "Svar (a=aksepter, d=avslå): " << std::flush;
                }
                std::string ans;
                if (!std::getline(std::cin, ans)) {
                    return false;
                }
                chat::trim_in_place(ans);
                if (ans == "a" || ans == "A") {
                    if (!direct.accept_invite(sid)) {
                        print_error("Kunne ikke akseptere (finnes session? er den pending?).");
                    }
                } else if (ans == "d" || ans == "D") {
                    {
                        std::lock_guard lock{chat::cout_mutex};
                        std::cout << "Valgfri grunn (tom = ingen): " << std::flush;
                    }
                    std::string reason;
                    if (!std::getline(std::cin, reason)) {
                        return false;
                    }
                    chat::trim_in_place(reason);
                    if (!direct.decline_invite(sid, reason)) {
                        print_error("Kunne ikke avslå (finnes session? er den pending?).");
                    }
                } else {
                    print_error("Ugyldig svar (bruk a eller d).");
                }
            }
                break;
            case 10:
            {
                const auto act = direct.snapshot_active_sessions();
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "\n--- Aktive private samtaler (" << act.size() << ") ---\n";
                    if (act.empty()) {
                        std::cout << "  (ingen)\n" << std::flush;
                    } else {
                        for (const auto& s : act) {
                            std::cout << "  Session " << s.session_id << " med " << s.peer_username
                                      << " @ " << s.peer_ip << '\n';
                        }
                        std::cout << std::flush;
                    }
                }
                if (act.empty()) {
                    break;
                }
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "Session-id:\n> " << std::flush;
                }
                std::string sid;
                if (!std::getline(std::cin, sid)) {
                    return false;
                }
                chat::trim_in_place(sid);
                if (sid.empty()) {
                    break;
                }
                if (sid.size() > 32) {
                    print_error("Session-id er for lang.");
                    break;
                }
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "Melding (tom linje = avbryt):\n> " << std::flush;
                }
                std::string msg;
                if (!std::getline(std::cin, msg)) {
                    return false;
                }
                chat::trim_in_place(msg);
                if (msg.empty()) {
                    break;
                }
                if (msg.size() > chat::kMaxChatMessageBytes) {
                    print_error("Meldingen er for lang.");
                    break;
                }
                if (!direct.send_private_chat(sid, msg)) {
                    print_error("Kunne ikke sende (ukjent session eller ikke aktiv).");
                } else {
                    print_line("[Privat " + sid + "] " + username + ": " + msg);
                }
            }
                break;
            case 11:
            {
                const auto act = direct.snapshot_active_sessions();
                std::lock_guard lock{chat::cout_mutex};
                std::cout << "\n--- Aktive private samtaler (" << act.size() << ") ---\n";
                if (act.empty()) {
                    std::cout << "  (ingen)\n" << std::flush;
                } else {
                    for (const auto& s : act) {
                        std::cout << "  Session " << s.session_id << " med " << s.peer_username
                                  << " @ " << s.peer_ip << '\n';
                    }
                    std::cout << std::flush;
                }
            }
                break;
            case 12:
                return true;
            default:
                break;
        }
    }
}

}  // namespace chat
