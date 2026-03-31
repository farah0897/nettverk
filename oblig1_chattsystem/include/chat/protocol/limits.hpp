#pragma once

#include <cstddef>

namespace chat {

inline constexpr std::size_t kMaxUsernameBytes = 32;
inline constexpr std::size_t kMaxChatMessageBytes = 512;
inline constexpr std::size_t kMaxRoomNameBytes = 32;  // RFC: SHOULD be short (<= 32 chars)
inline constexpr std::size_t kMaxPacketBytes = 1024;

/// RFC: Educational simplification — all open multicast rooms may share one group.
inline constexpr char kOpenRoomMulticastGroup[] = "239.0.0.1";

}  // namespace chat
