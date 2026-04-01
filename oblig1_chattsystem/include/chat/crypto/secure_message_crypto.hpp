#pragma once

/// TCP-nyttelast: encrypt-then-MAC (ChaCha20, HMAC-SHA256). Blob: 12 B nonce + ciphertext + 32 B MAC.
/// Avhengig av: random_bytes, chacha20, sha256_hmac. Socket-laget kaller bare `seal_message` / `open_message`.
///
/// Begrensninger (øving): PSK i klartekst på UDP; ingen forward secrecy; ingen replay-beskyttelse;
/// MAC-sammenligning er enkel byte-loop; ikke side-channel-hardened.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chat::crypto {

/// `psk`: 32 byte. Returnerer false ved f.eks. feilet tilfeldighetstrekk.
[[nodiscard]] bool seal_message(std::span<const std::uint8_t, 32> psk,
                                std::span<const std::uint8_t> plaintext,
                                std::vector<std::uint8_t>& sealed_out);

/// Verifiser MAC, deretter dekrypter. Ved feil: false og `plaintext_out` tømmes.
[[nodiscard]] bool open_message(std::span<const std::uint8_t, 32> psk,
                                std::span<const std::uint8_t> sealed,
                                std::vector<std::uint8_t>& plaintext_out);

}  // namespace chat::crypto
