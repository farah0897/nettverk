#pragma once

#include <cstdint>

namespace chat {

/// SECP / RFC USNChat01
inline constexpr std::uint16_t kUdpPort = 50000;  // all UDP communication
inline constexpr std::uint16_t kTcpPort = 50001;  // guaranteed rooms (TCP)

}  // namespace chat
