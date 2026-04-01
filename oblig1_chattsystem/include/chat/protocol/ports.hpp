#pragma once

#include <cstdint>

namespace chat {

/// SECP / RFC USNChat01
inline constexpr std::uint16_t kUdpPort = 50000;  // all UDP communication
inline constexpr std::uint16_t kTcpPort = 50001;  // guaranteed open rooms (TCP)
inline constexpr std::uint16_t kTcpSecurePort = 50002;  // sikre rom (TCP + krypto), adskilt fra 50001

}  // namespace chat
