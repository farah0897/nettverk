#include "chat/app/text_menu.hpp"

#include "chat/app/sync_output.hpp"
#include "chat/protocol/limits.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/services/direct_message_service.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/services/guaranteed_open_room_service.hpp"
#include "chat/services/secure_room_service.hpp"
#include "chat/services/lobby_service.hpp"
#include "chat/state/user_directory.hpp"
#include "chat/util/string_trim.hpp"

#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>

namespace {

const char* secure_phase_label(chat::SecureRoomPhase p) {
    switch (p) {
        case chat::SecureRoomPhase::Idle:
            return "Idle (ingen sikker sesjon)";
        case chat::SecureRoomPhase::InviteePending:
            return "InviteePending (venter på svar / UDP-invitasjon mottatt)";
        case chat::SecureRoomPhase::HostListening:
            return "HostListening (vert lytter på TCP, venter på klient)";
        case chat::SecureRoomPhase::HostHandshakePending:
            return "HostHandshakePending (TCP tilkoblet, håndtrykk ikke ferdig)";
        case chat::SecureRoomPhase::HostActive:
            return "HostActive (sikker chat aktiv som vert)";
        case chat::SecureRoomPhase::InviteeActive:
            return "InviteeActive (sikker chat aktiv som invitert)";
        default:
            return "?";
    }
}

bool validate_username_field(const std::string& s, std::string& err) {
    if (s.empty()) {
        err = "Brukernavn kan ikke være tomt.";
        return false;
    }
    if (s.size() > chat::kMaxUsernameBytes) {
        err = "Brukernavn er for langt.";
        return false;
    }
    for (char c : s) {
        if (c == '|' || c == '\n' || c == '\r') {
            err = "Brukernavn kan ikke inneholde | eller linjeskift.";
            return false;
        }
    }
    return true;
}

bool validate_room_name_field(const std::string& s, std::string& err) {
    if (s.empty()) {
        err = "Romnavn kan ikke være tomt.";
        return false;
    }
    if (s.size() > chat::kMaxRoomNameBytes) {
        err = "Romnavnet er for langt.";
        return false;
    }
    for (char c : s) {
        if (c == '|' || c == '\n' || c == '\r') {
            err = "Romnavn kan ikke inneholde | eller linjeskift.";
            return false;
        }
    }
    return true;
}

bool validate_session_id_field(const std::string& s, std::string& err) {
    if (s.empty()) {
        err = "Session-id kan ikke være tom.";
        return false;
    }
    if (s.size() > 32) {
        err = "Session-id er for lang.";
        return false;
    }
    for (unsigned char c : s) {
        if (!std::isxdigit(static_cast<int>(c))) {
            err = "Session-id skal være heksadesimale tegn (0-9, a-f).";
            return false;
        }
    }
    return true;
}

void print_menu() {
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n        MENY \n"
              << "UDP / oblig 1 velg (1-11):\n"
              << "  1. Vis aktive brukere\n"
              << "  2. Send melding til USN Chat\n"
              << "  3. Opprett åpent grupperom (UDP multicast)\n"
              << "  4. Vis tilgjengelige UDP multicast-grupperom\n"
              << "  5. Bli med i UDP multicast-grupperom\n"
              << "  6. Send melding til UDP multicast-grupperom\n"
              << "  7. Forlat UDP multicast-grupperom\n"
              << "  8. Inviter bruker til lukket 1-1 rom (UDP)\n"
              << "  9. Svar på UDP-invitasjon (aksept/avslag)\n"
              << " 10. Send privat melding (UDP lukket rom)\n"
              << " 11. Vis aktive private samtaler (UDP)\n"
              << "--------------------------------------\n"
              << "TCP garantert rom, port " << chat::kTcpPort << " velg (12-14):\n"
              << " 12. Opprett åpent garantert rom (du blir vert)\n"
              << " 13. Vis tilgjengelige åpne garanterte rom\n"
              << " 14. Bli med i åpent garantert rom (TCP-klient)\n"
              << "--------------------------------------\n"
              << "TCP sikkert rom, port " << chat::kTcpSecurePort << " velg (15-23):\n"
              << " 15. Opprett sikkert rom og inviter bruker (vert)\n"
              << " 16. Inviter bruker til sikkert rom (samme som 15)\n"
              << " 17. Svar på invitasjon til sikkert rom (aksept/avslå)\n"
              << " 18. Send melding i åpent garantert rom (TCP)\n"
              << " 19. Send melding i sikkert rom\n"
              << " 20. Vis aktive TCP-forbindelser / romstatus\n"
              << " 21. Forlat sikkert rom\n"
              << " 22. Forlat åpent garantert TCP-rom / vert\n"
              << " 23. Avslutt program\n"
              << "--------------------------------------\n"
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

bool read_trimmed_line(const std::string& prompt, std::string& out) {
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
        std::cout << "  " << p.username << " @ " << p.ipv4 << ", sist sett " << ago << " s siden\n";
    }
    std::cout.flush();
}

void print_udp_multicast_list(chat::GroupRoomCoordinator& groups) {
    auto list = groups.list_advertised_rooms();
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n--- UDP multicast-grupperom (" << list.size() << "), oblig 1 ---\n";
    if (list.empty()) {
        std::cout << "  (ingen)\n";
    } else {
        for (const auto& r : list) {
            std::cout << "  Rom: " << r.room_name << " @ " << r.multicast_ip << ":" << r.multicast_port
                      << '\n';
        }
    }
    std::cout.flush();
}

void print_guaranteed_tcp_list_only(chat::GuaranteedOpenRoomService& guaranteed_open) {
    auto tcp = guaranteed_open.list_advertised_tcp_rooms();
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n  Garanterte TCP-rom (UDP-annonser), oblig 2\n";
    if (tcp.empty()) {
        std::cout << "  (ingen i listen akkurat nå)\n";
    } else {
        for (const auto& r : tcp) {
            std::cout << "  Rom: " << r.room_name << ", vert " << r.owner_username << " @ TCP "
                      << r.owner_ipv4 << ":" << r.tcp_port << '\n';
        }
    }
    std::cout.flush();
}

void print_tcp_and_room_status(chat::GuaranteedOpenRoomService& guaranteed_open,
                               chat::SecureRoomService& secure_room, chat::GroupRoomCoordinator& groups) {
    std::lock_guard lock{chat::cout_mutex};
    std::cout << "\n  Status: TCP / rom\n";
    std::cout << "Garantert TCP (" << chat::kTcpPort << "):\n";
    std::cout << "  Vert (hosting): " << (guaranteed_open.is_hosting() ? "ja" : "nei");
    if (guaranteed_open.is_hosting()) {
        if (const auto n = guaranteed_open.active_room_name()) {
            std::cout << ", romnavn \"" << *n << "\"";
        }
    }
    std::cout << "\n  TCP-klient: " << (guaranteed_open.is_tcp_client() ? "ja" : "nei");
    if (guaranteed_open.is_tcp_client()) {
        if (const auto n = guaranteed_open.active_room_name()) {
            std::cout << ", rom \"" << *n << "\"";
        }
    }
    std::cout << "\n  Aktiv TCP-sesjon: " << (guaranteed_open.has_active_tcp_room() ? "ja" : "nei") << "\n";

    std::cout << "Sikkert rom (" << chat::kTcpSecurePort << "):\n";
    std::cout << "  Fase: " << secure_phase_label(secure_room.phase()) << "\n";
    std::cout << "  Aktiv sikker chat: " << (secure_room.has_active_secure_session() ? "ja" : "nei");
    if (const auto sid = secure_room.active_session_id()) {
        std::cout << ", session " << *sid;
    }
    std::cout << "\n  Ventende sikker INVITE: " << (secure_room.has_pending_secure_invite() ? "ja" : "nei")
              << "\n";

    std::cout << "UDP multicast-gruppe: " << (groups.has_active_membership() ? "medlem" : "ikke medlem");
    if (groups.has_active_membership()) {
        if (const auto id = groups.active_room_id()) {
            std::cout << ", \"" << *id << "\"";
        }
    }
    std::cout << "                       \n" << std::flush;
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
    if (v < 1 || v > 23) {
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

bool prompt_create_group(chat::GroupRoomCoordinator& groups, chat::GuaranteedOpenRoomService& guaranteed_open) {
    std::string name;
    if (!read_trimmed_line("Navn på UDP multicast-grupperom: ", name)) {
        return false;
    }
    std::string err;
    if (!validate_room_name_field(name, err)) {
        print_error(err);
        return true;
    }
    if (guaranteed_open.is_hosting()) {
        guaranteed_open.stop_hosting();
    } else {
        guaranteed_open.leave_tcp_room();
    }
    if (!groups.create_room(name)) {
        print_error("Kunne ikke opprette grupperom (nettverk/port opptatt?).");
        return true;
    }
    if (const auto id = groups.active_room_id()) {
        print_line("UDP multicast-grupperom opprettet. Romnavn: " + *id);
    }
    return true;
}

bool prompt_join_udp_multicast_only(chat::GroupRoomCoordinator& groups,
                                    chat::GuaranteedOpenRoomService& guaranteed_open) {
    std::string room_name;
    if (!read_trimmed_line("Romnavn (bruk 4 for oversikt): ", room_name)) {
        return false;
    }
    std::string err;
    if (!validate_room_name_field(room_name, err)) {
        print_error(err);
        return true;
    }
    if (guaranteed_open.is_hosting()) {
        guaranteed_open.stop_hosting();
    } else {
        guaranteed_open.leave_tcp_room();
    }
    if (!groups.join_room(room_name)) {
        print_error("Kunne ikke bli med (ukjent rom, allerede medlem, eller feil).");
        return true;
    }
    print_line("Du er nå med i UDP multicast-rom \"" + room_name + "\".");
    return true;
}

bool prompt_join_guaranteed_tcp_only(chat::GuaranteedOpenRoomService& guaranteed_open) {
    std::string room_name;
    if (!read_trimmed_line("Navn på garantert TCP-rom (bruk 13 for liste): ", room_name)) {
        return false;
    }
    std::string err;
    if (!validate_room_name_field(room_name, err)) {
        print_error(err);
        return true;
    }
    if (guaranteed_open.is_hosting()) {
        guaranteed_open.stop_hosting();
    } else {
        guaranteed_open.leave_tcp_room();
    }

    const auto tcp_rooms = guaranteed_open.list_advertised_tcp_rooms();
    for (const auto& tr : tcp_rooms) {
        if (tr.room_name == room_name) {
            if (!guaranteed_open.join_as_client(room_name)) {
                print_error("Kunne ikke koble TCP (vert utilgjengelig eller nettverksfeil).");
                return true;
            }
            print_line("Du er koblet til åpent garantert TCP-rom \"" + room_name + "\".");
            return true;
        }
    }
    print_error("Fant ikke rommet i annonserte garanterte TCP-rom (bruk 13, eller vent på annonse).");
    return true;
}

bool prompt_udp_multicast_chat_only(const std::string& username, chat::GroupRoomCoordinator& groups) {
    if (!groups.has_active_membership()) {
        print_error("Du er ikke med i et UDP multicast-grupperom (bruk 5).");
        return true;
    }
    std::string msg;
    if (!read_trimmed_line("Melding til UDP multicast-grupperom (tom = avbryt):\n> ", msg)) {
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

bool prompt_send_guaranteed_tcp_chat(const std::string& username,
                                     chat::GuaranteedOpenRoomService& guaranteed_open) {
    if (!guaranteed_open.has_active_tcp_room()) {
        print_error("Ingen aktiv tilkobling til åpent garantert rom (bruk 12 som vert eller 14 som klient).");
        return true;
    }
    std::string msg;
    if (!read_trimmed_line("Melding til garantert TCP-rom (tom = avbryt):\n> ", msg)) {
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
    const auto room_id = guaranteed_open.active_room_name();
    if (!room_id) {
        print_error("Ingen aktiv TCP-gruppe.");
        return true;
    }
    if (!guaranteed_open.send_chat(msg)) {
        print_error("Sending på TCP feilet.");
        return true;
    }
    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[TCP-rom " << *room_id << "] " << username << ": " << msg << '\n' << std::flush;
    }
    return true;
}

bool prompt_create_guaranteed_tcp(chat::GuaranteedOpenRoomService& guaranteed_open) {
    std::string name;
    if (!read_trimmed_line("Navn på åpent garantert TCP-rom: ", name)) {
        return false;
    }
    std::string err;
    if (!validate_room_name_field(name, err)) {
        print_error(err);
        return true;
    }
    if (!guaranteed_open.start_hosting(name)) {
        print_error("Kunne ikke starte TCP-vert (sjekk at port 50001 er ledig).");
        return true;
    }
    print_line("Åpent garantert rom opprettet. Du er vert på TCP " + std::to_string(chat::kTcpPort) +
               ". Annonse sendes på UDP broadcast.");
    return true;
}

bool prompt_secure_invite(chat::SecureRoomService& secure_room) {
    std::string who;
    if (!read_trimmed_line("Brukernavn å invitere (sikkert rom, vert på TCP " +
                               std::to_string(chat::kTcpSecurePort) + "):\n> ",
                           who)) {
        return false;
    }
    std::string err;
    if (!validate_username_field(who, err)) {
        print_error(err);
        return true;
    }
    if (!secure_room.send_secure_invite(who)) {
        print_error("Kunne ikke starte sikkert rom (ukjent bruker, port opptatt eller nettverk).");
        return true;
    }
    print_line("Sikker invitasjon sendt på UDP. Du lytter på TCP " + std::to_string(chat::kTcpSecurePort) +
               ". Mottaker bruker meny 17.");
    return true;
}

bool prompt_secure_accept_or_decline(chat::SecureRoomService& secure_room) {
    const auto pending = secure_room.list_pending_secure_invites();
    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "\n  Ventende sikre invitasjoner (" << pending.size() << ")\n";
        if (pending.empty()) {
            std::cout << "  (ingen)\n" << std::flush;
        } else {
            for (const auto& p : pending) {
                std::cout << "  Session " << p.session_id << " fra " << p.from_username << '\n';
            }
            std::cout << std::flush;
        }
    }
    if (pending.empty()) {
        return true;
    }
    std::string sid;
    if (!read_trimmed_line("Session-id (fra listen over, tom = avbryt):\n> ", sid)) {
        return false;
    }
    if (sid.empty()) {
        return true;
    }
    std::string err;
    if (!validate_session_id_field(sid, err)) {
        print_error(err);
        return true;
    }
    std::string ans;
    if (!read_trimmed_line("Svar (a=aksepter, d=avslå): ", ans)) {
        return false;
    }
    chat::trim_in_place(ans);
    if (ans == "a" || ans == "A") {
        if (!secure_room.accept_secure_invite(sid)) {
            print_error("Kunne ikke koble til (feil session, vert utilgjengelig eller TCP-feil).");
        } else {
            print_line("Koblet til sikkert rom. Bruk meny 19 for å chatte.");
        }
    } else if (ans == "d" || ans == "D") {
        secure_room.decline_secure_invite(sid);
        print_line("Invitasjon avslått.");
    } else {
        print_error("Ugyldig svar (bruk a eller d).");
    }
    return true;
}

bool prompt_send_secure_chat(const std::string& username, chat::SecureRoomService& secure_room) {
    if (!secure_room.has_active_secure_session()) {
        print_error("Ingen aktiv sikker sesjon (vert: 15/16, invitert: 17).");
        return true;
    }
    std::string msg;
    if (!read_trimmed_line("Melding til sikkert rom (tom = avbryt):\n> ", msg)) {
        return false;
    }
    if (msg.empty()) {
        return true;
    }
    if (msg.size() > chat::kMaxChatMessageBytes) {
        print_error("Meldingen er for lang.");
        return true;
    }
    if (!secure_room.send_secure_chat(msg)) {
        print_error("Kunne ikke sende (forbindelse lukket?).");
        return true;
    }
    if (const auto sid = secure_room.active_session_id()) {
        print_line("[Sikkert " + *sid + "] " + username + ": " + msg);
    }
    return true;
}

}  // namespace

namespace chat {

bool run_text_menu_loop(const std::string& username, UserDirectory& users, LobbyService& lobby,
                        GroupRoomCoordinator& groups, DirectMessageService& direct,
                        GuaranteedOpenRoomService& guaranteed_open, SecureRoomService& secure_room) {
    std::string line;
    while (true) {
        print_menu();
        if (!std::getline(std::cin, line)) {
            print_error("(stdin lukket, avslutter.)");
            return false;
        }
        chat::trim_in_place(line);
        const auto choice = parse_menu_choice(line);
        if (!choice) {
            print_error("Ugyldig valg. Skriv heltall 1-23 (23 = avslutt).");
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
                if (!prompt_create_group(groups, guaranteed_open)) {
                    return false;
                }
                break;
            case 4:
                print_udp_multicast_list(groups);
                break;
            case 5:
                if (!prompt_join_udp_multicast_only(groups, guaranteed_open)) {
                    return false;
                }
                break;
            case 6:
                if (!prompt_udp_multicast_chat_only(username, groups)) {
                    return false;
                }
                break;
            case 7:
                groups.leave_room();
                print_line("Forlatt UDP multicast-grupperom (TCP-forbindelser påvirkes ikke).");
                break;
            case 8: {
                std::string who;
                if (!read_trimmed_line("Hvem vil du invitere? (bruk 1 for aktive brukere)\n> ", who)) {
                    return false;
                }
                std::string err;
                if (!validate_username_field(who, err)) {
                    print_error(err);
                    break;
                }
                const auto sid = direct.invite_user(who);
                if (!sid) {
                    print_error("Kunne ikke sende invitasjon (ukjent bruker / nettverksfeil).");
                } else {
                    print_line("Invitasjon sendt. Session-ID: " + *sid);
                }
                break;
            }
            case 9: {
                const auto pending = direct.snapshot_pending_invites();
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "\n  Pending UDP-invitasjoner (" << pending.size() << ")\n";
                    if (pending.empty()) {
                        std::cout << "  (ingen)\n" << std::flush;
                    } else {
                        for (const auto& p : pending) {
                            std::cout << "  Session " << p.session_id << " fra " << p.from_username << '\n';
                        }
                        std::cout << std::flush;
                    }
                }
                if (pending.empty()) {
                    break;
                }
                std::string sid;
                if (!read_trimmed_line("Session-id (tom = avbryt):\n> ", sid)) {
                    return false;
                }
                if (sid.empty()) {
                    break;
                }
                if (sid.size() > 32) {
                    print_error("Session-id er for lang.");
                    break;
                }
                std::string ans;
                if (!read_trimmed_line("Svar (a=aksepter, d=avslå): ", ans)) {
                    return false;
                }
                chat::trim_in_place(ans);
                if (ans == "a" || ans == "A") {
                    if (!direct.accept_invite(sid)) {
                        print_error("Kunne ikke akseptere (finnes session? er den pending?).");
                    }
                } else if (ans == "d" || ans == "D") {
                    std::string reason;
                    if (!read_trimmed_line("Valgfri grunn (tom = ingen): ", reason)) {
                        return false;
                    }
                    chat::trim_in_place(reason);
                    if (!direct.decline_invite(sid, reason)) {
                        print_error("Kunne ikke avslå (finnes session? er den pending?).");
                    }
                } else {
                    print_error("Ugyldig svar (bruk a eller d).");
                }
                break;
            }
            case 10: {
                const auto act = direct.snapshot_active_sessions();
                {
                    std::lock_guard lock{chat::cout_mutex};
                    std::cout << "\n--- Aktive private samtaler (" << act.size() << ") ---\n";
                    if (act.empty()) {
                        std::cout << "  (ingen)\n" << std::flush;
                    } else {
                        for (const auto& s : act) {
                            std::cout << "  Session " << s.session_id << " med " << s.peer_username << " @ "
                                      << s.peer_ip << '\n';
                        }
                        std::cout << std::flush;
                    }
                }
                if (act.empty()) {
                    break;
                }
                std::string sid;
                if (!read_trimmed_line("Session-id:\n> ", sid)) {
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
                std::string msg;
                if (!read_trimmed_line("Melding (tom = avbryt):\n> ", msg)) {
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
                break;
            }
            case 11: {
                const auto act = direct.snapshot_active_sessions();
                std::lock_guard lock{chat::cout_mutex};
                std::cout << "\n  Aktive private samtaler (" << act.size() << ")\n";
                if (act.empty()) {
                    std::cout << "  (ingen)\n" << std::flush;
                } else {
                    for (const auto& s : act) {
                        std::cout << "  Session " << s.session_id << " med " << s.peer_username << " @ "
                                  << s.peer_ip << '\n';
                    }
                    std::cout << std::flush;
                }
                break;
            }
            case 12:
                if (!prompt_create_guaranteed_tcp(guaranteed_open)) {
                    return false;
                }
                break;
            case 13:
                print_guaranteed_tcp_list_only(guaranteed_open);
                break;
            case 14:
                if (!prompt_join_guaranteed_tcp_only(guaranteed_open)) {
                    return false;
                }
                break;
            case 15:
            case 16:
                if (!prompt_secure_invite(secure_room)) {
                    return false;
                }
                break;
            case 17:
                if (!prompt_secure_accept_or_decline(secure_room)) {
                    return false;
                }
                break;
            case 18:
                if (!prompt_send_guaranteed_tcp_chat(username, guaranteed_open)) {
                    return false;
                }
                break;
            case 19:
                if (!prompt_send_secure_chat(username, secure_room)) {
                    return false;
                }
                break;
            case 20:
                print_tcp_and_room_status(guaranteed_open, secure_room, groups);
                break;
            case 21:
                secure_room.leave_secure_session();
                print_line("Forlatt sikkert rom (TCP " + std::to_string(chat::kTcpSecurePort) + ").");
                break;
            case 22:
                if (guaranteed_open.is_hosting()) {
                    guaranteed_open.stop_hosting();
                    print_line("Stoppet vert for åpent garantert TCP-rom.");
                } else {
                    guaranteed_open.leave_tcp_room();
                    print_line("Forlatt åpent garantert TCP-rom (klient).");
                }
                break;
            case 23:
                return true;
            default:
                break;
        }
    }
}

}  // namespace chat
