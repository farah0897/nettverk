#pragma once

#include "chat/protocol/limits.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace chat {

/// RFC USNChat01 / SECP
enum class SecpType { Presence, RoomAnnounce, Invite, Chat, Unknown };

struct SecpMessage {
    SecpType type{SecpType::Unknown};
    std::string room;      // room name or "-" if not applicable
    std::string username;  // sender/owner
    std::string payload;   // message-specific
};

/// Build a line-delimited message: TYPE|ROOM|USERNAME|PAYLOAD\n
/// Returns empty string if inputs are invalid (length, separators, etc).
std::string build_secp_line(SecpType type, std::string_view room, std::string_view username,
                            std::string_view payload);

/// Parse SECP line (datagram contents). Accepts with or without trailing '\n'.
/// Returns nullopt on malformed/invalid input.
std::optional<SecpMessage> parse_secp_line(std::string_view line);

/// Utility: converts enum to TYPE string.
std::string_view secp_type_name(SecpType t);

/// Utility: parse TYPE token (e.g. "PRESENCE").
SecpType parse_secp_type(std::string_view token);

}  // namespace chat

