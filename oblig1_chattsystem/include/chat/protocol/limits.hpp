#pragma once

#include <cstddef>
#include <string_view>

namespace chat {

inline constexpr std::size_t kMaxUsernameBytes = 32;
inline constexpr std::size_t kMaxChatMessageBytes = 512;
inline constexpr std::size_t kMaxRoomNameBytes = 32;  // RFC: SHOULD be short (<= 32 chars)
inline constexpr std::size_t kMaxPacketBytes = 1024;

/// Første applikasjonsmelding på sikker TCP (inni krypto): `CHAT|ROOM|USERNAME|PAYLOAD` med denne PAYLOAD.
inline constexpr std::string_view kSecureTcpHandshakePayload{"__SECURE_HANDSHAKE__"};

/// Felles multicast-gruppe for alle åpne grupperom (forenkling).
inline constexpr char kOpenRoomMulticastGroup[] = "239.0.0.1";

}  // namespace chat
