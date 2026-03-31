#include "chat/protocol/secp.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace chat {

namespace {

bool valid_utf8(std::string_view s) {
    // Minimal, strict UTF-8 validation (rejects overlongs and surrogates).
    const auto* p = reinterpret_cast<const std::uint8_t*>(s.data());
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const std::uint8_t b0 = p[i];
        if (b0 <= 0x7F) {
            ++i;
            continue;
        }
        auto cont = [&](std::size_t j) -> bool {
            return j < n && (p[j] & 0xC0u) == 0x80u;
        };

        if (b0 >= 0xC2 && b0 <= 0xDF) {
            if (!cont(i + 1)) return false;
            i += 2;
            continue;
        }
        if (b0 == 0xE0) {
            if (!(i + 2 < n)) return false;
            const std::uint8_t b1 = p[i + 1], b2 = p[i + 2];
            if (!(b1 >= 0xA0 && b1 <= 0xBF)) return false;  // no overlong
            if ((b2 & 0xC0u) != 0x80u) return false;
            i += 3;
            continue;
        }
        if (b0 >= 0xE1 && b0 <= 0xEC) {
            if (!cont(i + 1) || !cont(i + 2)) return false;
            i += 3;
            continue;
        }
        if (b0 == 0xED) {
            if (!(i + 2 < n)) return false;
            const std::uint8_t b1 = p[i + 1], b2 = p[i + 2];
            if (!(b1 >= 0x80 && b1 <= 0x9F)) return false;  // reject surrogates
            if ((b2 & 0xC0u) != 0x80u) return false;
            i += 3;
            continue;
        }
        if (b0 >= 0xEE && b0 <= 0xEF) {
            if (!cont(i + 1) || !cont(i + 2)) return false;
            i += 3;
            continue;
        }
        if (b0 == 0xF0) {
            if (!(i + 3 < n)) return false;
            const std::uint8_t b1 = p[i + 1], b2 = p[i + 2], b3 = p[i + 3];
            if (!(b1 >= 0x90 && b1 <= 0xBF)) return false;  // no overlong
            if ((b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) return false;
            i += 4;
            continue;
        }
        if (b0 >= 0xF1 && b0 <= 0xF3) {
            if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return false;
            i += 4;
            continue;
        }
        if (b0 == 0xF4) {
            if (!(i + 3 < n)) return false;
            const std::uint8_t b1 = p[i + 1], b2 = p[i + 2], b3 = p[i + 3];
            if (!(b1 >= 0x80 && b1 <= 0x8F)) return false;  // <= U+10FFFF
            if ((b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) return false;
            i += 4;
            continue;
        }
        return false;
    }
    return true;
}

bool contains_pipe_or_newline(std::string_view s) {
    for (const char c : s) {
        if (c == '|' || c == '\n' || c == '\r') {
            return true;
        }
    }
    return false;
}

bool is_room_ok(std::string_view room) {
    if (room == "-") {
        return true;
    }
    if (room.empty() || room.size() > kMaxRoomNameBytes) {
        return false;
    }
    return !contains_pipe_or_newline(room) && valid_utf8(room);
}

bool is_username_ok(std::string_view u) {
    if (u.empty() || u.size() > kMaxUsernameBytes) {
        return false;
    }
    return !contains_pipe_or_newline(u) && valid_utf8(u);
}

bool is_payload_ok(std::string_view p) {
    if (p == "-") {
        return true;
    }
    if (p.size() > kMaxChatMessageBytes) {  // payload is often chat text
        return false;
    }
    return !contains_pipe_or_newline(p) && valid_utf8(p);
}

}  // namespace

std::string_view secp_type_name(SecpType t) {
    switch (t) {
        case SecpType::Presence:
            return "PRESENCE";
        case SecpType::RoomAnnounce:
            return "ROOM_ANNOUNCE";
        case SecpType::Invite:
            return "INVITE";
        case SecpType::Chat:
            return "CHAT";
        default:
            return "UNKNOWN";
    }
}

SecpType parse_secp_type(std::string_view token) {
    if (token == "PRESENCE") {
        return SecpType::Presence;
    }
    if (token == "ROOM_ANNOUNCE") {
        return SecpType::RoomAnnounce;
    }
    if (token == "INVITE") {
        return SecpType::Invite;
    }
    if (token == "CHAT") {
        return SecpType::Chat;
    }
    return SecpType::Unknown;
}

std::string build_secp_line(SecpType type, std::string_view room, std::string_view username,
                            std::string_view payload) {
    if (type == SecpType::Unknown) {
        return {};
    }
    if (!is_room_ok(room) || !is_username_ok(username) || !is_payload_ok(payload)) {
        return {};
    }
    std::string out;
    out.reserve(16 + room.size() + username.size() + payload.size());
    out.append(secp_type_name(type));
    out.push_back('|');
    out.append(room);
    out.push_back('|');
    out.append(username);
    out.push_back('|');
    out.append(payload);
    out.push_back('\n');
    if (out.size() > kMaxPacketBytes) {
        return {};
    }
    return out;
}

std::optional<SecpMessage> parse_secp_line(std::string_view line) {
    if (line.size() > kMaxPacketBytes || line.empty()) {
        return std::nullopt;
    }
    // strip trailing newline(s)
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return std::nullopt;
    }

    // split into exactly 4 fields
    std::array<std::string_view, 4> parts{};
    std::size_t idx = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '|') {
            if (idx >= parts.size()) {
                return std::nullopt;
            }
            parts[idx++] = line.substr(start, i - start);
            start = i + 1;
        }
    }
    if (idx != 4) {
        return std::nullopt;
    }

    const SecpType t = parse_secp_type(parts[0]);
    if (t == SecpType::Unknown) {
        return std::nullopt;
    }
    if (!is_room_ok(parts[1]) || !is_username_ok(parts[2]) || !is_payload_ok(parts[3])) {
        return std::nullopt;
    }

    SecpMessage m;
    m.type = t;
    m.room.assign(parts[1].data(), parts[1].size());
    m.username.assign(parts[2].data(), parts[2].size());
    m.payload.assign(parts[3].data(), parts[3].size());
    return m;
}

}  // namespace chat

