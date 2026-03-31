#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <netinet/in.h>

namespace chat {

class UdpSocket;

/// Åpent felles rom: `LobbyChat` på `kLobbyBroadcastPort` (UDP broadcast).
class LobbyService {
public:
    explicit LobbyService(UdpSocket& socket);

    void set_on_message(std::function<void(const std::string& sender, const std::string& text)> cb);

    /// Tomt brukernavn/tekst eller for lang tekst → false (ingen send).
    bool send_chat(const std::string& username, const std::string& text);

    /// Behandler dekodet SECP CHAT for lobby ("USN Chat").
    void on_lobby_chat(const std::string& sender, const std::string& text);

private:
    UdpSocket* socket_{nullptr};
    std::function<void(const std::string&, const std::string&)> on_message_;
    sockaddr_in broadcast_dest_{};
};

}  // namespace chat
