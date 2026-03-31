#include "chat/state/user_directory.hpp"

#include "chat/net/local_endpoint.hpp"

#include <algorithm>

namespace chat {

void UserDirectory::set_self_username(std::string name) {
    std::lock_guard lock{mutex_};
    self_username_ = std::move(name);
}

void UserDirectory::upsert(const std::string& username, const sockaddr_in& from) {
    if (username.empty()) {
        return;
    }
    std::lock_guard lock{mutex_};
    if (!self_username_.empty() && username == self_username_) {
        return;
    }
    PeerInfo info;
    info.username = username;
    info.address = from;
    info.last_seen = std::chrono::steady_clock::now();
    peers_[username] = std::move(info);
}

void UserDirectory::prune_stale(std::chrono::seconds max_age) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock{mutex_};
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (now - it->second.last_seen > max_age) {
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<sockaddr_in> UserDirectory::find_address(const std::string& username) const {
    std::lock_guard lock{mutex_};
    const auto it = peers_.find(username);
    if (it == peers_.end()) {
        return std::nullopt;
    }
    return it->second.address;
}

std::vector<PeerListEntry> UserDirectory::snapshot_peers() const {
    std::lock_guard lock{mutex_};
    std::vector<PeerListEntry> out;
    out.reserve(peers_.size());
    for (const auto& [name, info] : peers_) {
        PeerListEntry e;
        e.username = name;
        e.ipv4 = format_ipv4(info.address.sin_addr);
        e.last_seen = info.last_seen;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const PeerListEntry& a, const PeerListEntry& b) {
        return a.username < b.username;
    });
    return out;
}

}  // namespace chat
