#pragma once

#include "chat/protocol/limits.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace chat {

/// SECP: én logisk melding per linje `TYPE|ROOM|USERNAME|PAYLOAD` + `\n` (RFC USNChat01).
/// Tekstfelt er UTF-8; `|` og linjeskift er forbudt i felt (unntatt avsluttende `\n` på linjen).
///
/// **UDP (50000):** Hele linjen sendes som ett datagram.
/// **TCP (f.eks. 50001/50002):** Samme linjeformat; innkapsling (f.eks. lengdeprefiks + krypto på 50002)
/// er transportlag; parse/build her er newline-terminert SECP.
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

/// Parse SECP-linje (UDP eller TCP-nyttelast). Aksepterer med eller uten avsluttende `\n`/`\r\n`.
/// Ukjente `TYPE`-token gir `nullopt` (behandle som «ignorer» / ugyldig).
/// `Unknown` brukes ikke i `SecpMessage`; typen må være kjent og felt valideres.
std::optional<SecpMessage> parse_secp_line(std::string_view line);

/// CLI-brukernavn: samme regler som SECP USERNAME-felt.
[[nodiscard]] bool is_valid_username_field(std::string_view u);

/// Utility: converts enum to TYPE string.
std::string_view secp_type_name(SecpType t);

/// Utility: parse TYPE token (e.g. "PRESENCE").
SecpType parse_secp_type(std::string_view token);

}  // namespace chat

