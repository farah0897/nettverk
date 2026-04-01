#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>

namespace chat {

struct PeerInfo {
    std::string username;
    sockaddr_in address{};
    std::chrono::steady_clock::time_point last_seen{};
};

struct PeerListEntry {
    std::string username;
    std::string ipv4;
    std::chrono::steady_clock::time_point last_seen;
};

/// Trådsikker katalog over oppdagede brukere (discovery).
class UserDirectory {
public:
    void set_self_username(std::string name);

    void upsert(const std::string& username, const sockaddr_in& from);

    void prune_stale(std::chrono::seconds max_age);

    std::optional<sockaddr_in> find_address(const std::string& username) const;

    /// Kopi under mutex; trygg å lese fra CLI.
    std::vector<PeerListEntry> snapshot_peers() const;

private:
    mutable std::mutex mutex_;
    std::string self_username_;
    std::unordered_map<std::string, PeerInfo> peers_;
};

}  // namespace chat
