#include "chat/app/receive_multiplex_loop.hpp"
#include "chat/app/interruptible_sleep.hpp"
#include "chat/app/sync_output.hpp"
#include "chat/app/text_menu.hpp"
#include "chat/log/logger.hpp"
#include "chat/net/local_endpoint.hpp"
#include "chat/net/errno_util.hpp"
#include "chat/net/udp_socket.hpp"
#include "chat/protocol/ports.hpp"
#include "chat/services/discovery_service.hpp"
#include "chat/services/direct_message_service.hpp"
#include "chat/services/group_room_coordinator.hpp"
#include "chat/services/lobby_service.hpp"
#include "chat/state/user_directory.hpp"
#include "chat/util/string_trim.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    using namespace chat;

    if (argc < 2) {
        std::cerr << "Bruk: " << (argc > 0 ? argv[0] : "chatapp") << " <brukernavn>\n";
        return 1;
    }
    std::string username = argv[1];
    trim_in_place(username);
    if (username.empty()) {
        std::cerr << "Brukernavn kan ikke være tomt.\n";
        return 1;
    }

    in_addr local_ip{};
    if (!get_local_ipv4(local_ip)) {
        std::cerr << "Kunne ikke finne lokal IPv4-adresse.\n";
        return 1;
    }

    UdpSocket udp_sock;
    if (!udp_sock.open()) {
        std::cerr << errno_message("socket(udp)") << "\n";
        return 1;
    }
    if (!udp_sock.set_reuse_address(true)) {
        std::cerr << errno_message("setsockopt(SO_REUSEADDR udp)", udp_sock.last_errno()) << "\n";
        return 1;
    }
    if (!udp_sock.set_broadcast(true)) {
        std::cerr << errno_message("setsockopt(SO_BROADCAST udp)", udp_sock.last_errno()) << "\n";
        return 1;
    }
    if (!udp_sock.bind(kUdpPort)) {
        std::cerr << errno_message("bind(udp)", udp_sock.last_errno()) << "\n";
        return 1;
    }

    UserDirectory users;
    users.set_self_username(username);

    DiscoveryService discovery{users, udp_sock};
    discovery.set_identity(username, local_ip);

    LobbyService lobby{udp_sock};
    lobby.set_on_message(
        [username](const std::string& sender, const std::string& text) {
            if (sender == username) {
                return;
            }
            std::lock_guard lock{chat::cout_mutex};
            std::cout << "[USN Chat] " << sender << ": " << text << '\n' << std::flush;
        });

    GroupRoomCoordinator groups(udp_sock, local_ip, username);
    groups.set_on_group_message([](const std::string& room_id, const std::string& sender,
                                   const std::string& text) {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[Gruppe " << room_id << "] " << sender << ": " << text << '\n' << std::flush;
    });

    DirectMessageService direct{udp_sock, users, username};
    direct.set_on_status([](const std::string& msg) {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[Privat] " << msg << '\n' << std::flush;
    });
    direct.set_on_private_message([](const std::string& sid, const std::string& sender,
                                     const std::string& text) {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "[Privat " << sid << "] " << sender << ": " << text << '\n' << std::flush;
    });

    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "Logget inn som \"" << username << "\" med IP " << format_ipv4(local_ip) << "\n";
        std::cout << "Porter: UDP " << kUdpPort << " (SECP), TCP " << kTcpPort << " (ikke i bruk).\n";
        std::cout << "Meldinger fra nett blandes i menyen.\n\n";
    }

    if (!discovery.send_presence_broadcast()) {
        std::cerr << "Advarsel: første presence-broadcast feilet.\n";
    } else {
        Logger::instance().info("startup: first presence broadcast sent");
    }

    std::atomic<bool> running{true};
    InterruptibleSleep sleeper;

    std::thread receiver{[&]() {
        run_receive_multiplex_loop(running, udp_sock, discovery, lobby, groups, direct,
                                   std::chrono::milliseconds{500});
    }};

    std::thread heartbeat{[&]() {
        while (running.load()) {
            if (!sleeper.sleep_for(kDiscoveryHeartbeatInterval)) {
                break;
            }
            discovery.send_presence_broadcast();
            discovery.prune_stale_peers();
            direct.prune_pending(std::chrono::seconds{30});
            direct.resend_outgoing_pending(std::chrono::seconds{6}, std::chrono::seconds{45});
        }
    }};

    std::thread group_advert_loop{[&]() {
        while (running.load()) {
            if (!sleeper.sleep_for(kGroupAdvertBroadcastInterval)) {
                break;
            }
            groups.broadcast_owned_advert();
        }
    }};

    run_text_menu_loop(username, users, lobby, groups, direct);

    running = false;
    sleeper.request_stop();
    udp_sock.close();
    groups.leave_room();
    if (receiver.joinable()) {
        receiver.join();
    }
    if (heartbeat.joinable()) {
        heartbeat.join();
    }
    if (group_advert_loop.joinable()) {
        group_advert_loop.join();
    }

    {
        std::lock_guard lock{chat::cout_mutex};
        std::cout << "Ha det.\n" << std::flush;
    }
    return 0;
}
