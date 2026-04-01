// seal/open: ChaCha20 + HMAC; ingen socket-kode her.
#include "chat/crypto/secure_message_crypto.hpp"

#include "chat/crypto/chacha20.hpp"
#include "chat/crypto/random_bytes.hpp"
#include "chat/crypto/sha256_hmac.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace chat::crypto {

namespace {

/// Avleder separat MAC-nøkkel fra PSK slik at samme 32-byte ikke brukes både til ChaCha og HMAC
/// Nøkkeldistinksjon (enkel KDF-lignende bruk).
void derive_mac_key(std::span<const std::uint8_t, 32> psk, std::span<std::uint8_t, 32> mac_key_out) {
    std::uint8_t buf[33];
    std::memcpy(buf, psk.data(), 32);
    buf[32] = 0x4d;  // skiller MAC-nøkkel fra stream-nøkkel
    sha256_hash(std::span<const std::uint8_t>(buf, 33), mac_key_out);
}

/// Konstant-tidsaktig nok for undervisning: ikke avkort tidlig ved første ulik byte.
bool const_time_equal_32(std::span<const std::uint8_t, 32> a, std::span<const std::uint8_t, 32> b) {
    std::uint8_t d = 0;
    for (std::size_t i = 0; i < 32; ++i) {
        d = static_cast<std::uint8_t>(d | (a[i] ^ b[i]));
    }
    return d == 0;
}

}  // namespace

bool seal_message(std::span<const std::uint8_t, 32> psk, std::span<const std::uint8_t> plaintext,
                  std::vector<std::uint8_t>& sealed_out) {
    sealed_out.clear();

    // 1) Nonce må være unik per melding med samme PSK.
    std::array<std::uint8_t, 12> nonce{};
    if (!fill_random(std::span<std::uint8_t>(nonce.data(), nonce.size()))) {
        return false;
    }

    // 2) Encrypt-then-MAC: først XOR (ChaCha20), MAC beregnes over nonce ‖ ciphertext.
    std::vector<std::uint8_t> ct(plaintext.begin(), plaintext.end());
    chacha20_xor(psk, nonce, 1, ct.data(), ct.size());

    std::array<std::uint8_t, 32> mac_key{};
    derive_mac_key(psk, mac_key);

    std::vector<std::uint8_t> mac_input;
    mac_input.reserve(12 + ct.size());
    mac_input.insert(mac_input.end(), nonce.begin(), nonce.end());
    mac_input.insert(mac_input.end(), ct.begin(), ct.end());

    std::array<std::uint8_t, 32> mac{};
    hmac_sha256(mac_key, mac_input, mac);

    sealed_out.reserve(12 + ct.size() + 32);
    sealed_out.insert(sealed_out.end(), nonce.begin(), nonce.end());
    sealed_out.insert(sealed_out.end(), ct.begin(), ct.end());
    sealed_out.insert(sealed_out.end(), mac.begin(), mac.end());
    return true;
}

bool open_message(std::span<const std::uint8_t, 32> psk, std::span<const std::uint8_t> sealed,
                  std::vector<std::uint8_t>& plaintext_out) {
    plaintext_out.clear();
    if (sealed.size() < 12 + 32) {
        return false;
    }

    const std::size_t ct_len = sealed.size() - 12 - 32;
    std::array<std::uint8_t, 12> nonce{};
    std::memcpy(nonce.data(), sealed.data(), 12);

    std::vector<std::uint8_t> ct(sealed.begin() + 12, sealed.begin() + 12 + static_cast<std::ptrdiff_t>(ct_len));
    const std::uint8_t* mac_incoming = sealed.data() + 12 + ct_len;

    std::array<std::uint8_t, 32> mac_key{};
    derive_mac_key(psk, mac_key);

    std::vector<std::uint8_t> mac_input;
    mac_input.reserve(12 + ct.size());
    mac_input.insert(mac_input.end(), nonce.begin(), nonce.end());
    mac_input.insert(mac_input.end(), ct.begin(), ct.end());

    std::array<std::uint8_t, 32> mac_calc{};
    hmac_sha256(mac_key, mac_input, mac_calc);

    std::array<std::uint8_t, 32> mac_received{};
    std::memcpy(mac_received.data(), mac_incoming, 32);
    if (!const_time_equal_32(mac_received, mac_calc)) {
        // Ved feil MAC: ikke prøv å dekryptere (unngår «padding oracle»-aktig feil i andre design).
        return false;
    }

    chacha20_xor(psk, nonce, 1, ct.data(), ct.size());
    plaintext_out = std::move(ct);
    return true;
}

}  // namespace chat::crypto
