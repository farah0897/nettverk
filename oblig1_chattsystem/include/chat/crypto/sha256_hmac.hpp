#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace chat::crypto {

/// SHA-256 av vilkårlig lengde data → 32 byte.
void sha256_hash(std::span<const std::uint8_t> data, std::span<std::uint8_t, 32> out_digest);

/// HMAC-SHA256 (RFC 2104). `out_mac` er alltid 32 byte.
void hmac_sha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message,
                 std::span<std::uint8_t, 32> out_mac);

}  // namespace chat::crypto
