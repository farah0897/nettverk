#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace chat::crypto {

/// ChaCha20 (RFC 7539). Gir ikke integritet alene; bruk med MAC i secure_message_crypto.
/// XOR-er buf med keystream. `counter_start` er vanligvis 1 (IETF) for første datablokk.
void chacha20_xor(std::span<const std::uint8_t, 32> key, std::span<const std::uint8_t, 12> nonce,
                  std::uint32_t counter_start, std::uint8_t* buf, std::size_t len);

}  // namespace chat::crypto
