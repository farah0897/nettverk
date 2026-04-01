// ChaCha20. Ingen socket/protokoll her.
#include "chat/crypto/chacha20.hpp"

#include <algorithm>
#include <cstring>

namespace chat::crypto {

namespace {

inline std::uint32_t load32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void store32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

inline std::uint32_t rotl32(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void quarter_round(std::uint32_t* x, int a, int b, int c, int d) {
    x[a] += x[b];
    x[d] ^= x[a];
    x[d] = rotl32(x[d], 16);
    x[c] += x[d];
    x[b] ^= x[c];
    x[b] = rotl32(x[b], 12);
    x[a] += x[b];
    x[d] ^= x[a];
    x[d] = rotl32(x[d], 8);
    x[c] += x[d];
    x[b] ^= x[c];
    x[b] = rotl32(x[b], 7);
}

// Én 64-byte keystream-blokk (20 «doble runder» = 10 iterasjoner av column+diagonal).
void chacha20_block(const std::uint8_t key[32], const std::uint8_t nonce[12], std::uint32_t counter,
                    std::uint8_t out[64]) {
    std::uint32_t st[16];
    st[0] = 0x61707865U;
    st[1] = 0x3320646eU;
    st[2] = 0x79622d32U;
    st[3] = 0x6b206574U;
    st[4] = load32_le(key + 0);
    st[5] = load32_le(key + 4);
    st[6] = load32_le(key + 8);
    st[7] = load32_le(key + 12);
    st[8] = load32_le(key + 16);
    st[9] = load32_le(key + 20);
    st[10] = load32_le(key + 24);
    st[11] = load32_le(key + 28);
    st[12] = counter;
    st[13] = load32_le(nonce + 0);
    st[14] = load32_le(nonce + 4);
    st[15] = load32_le(nonce + 8);

    std::uint32_t w[16];
    std::memcpy(w, st, sizeof(w));
    for (int i = 0; i < 10; ++i) {
        quarter_round(w, 0, 4, 8, 12);
        quarter_round(w, 1, 5, 9, 13);
        quarter_round(w, 2, 6, 10, 14);
        quarter_round(w, 3, 7, 11, 15);
        quarter_round(w, 0, 5, 10, 15);
        quarter_round(w, 1, 6, 11, 12);
        quarter_round(w, 2, 7, 8, 13);
        quarter_round(w, 3, 4, 9, 14);
    }
    for (int i = 0; i < 16; ++i) {
        st[i] += w[i];
    }
    for (int i = 0; i < 16; ++i) {
        store32_le(out + 4 * i, st[i]);
    }
}

}  // namespace

void chacha20_xor(std::span<const std::uint8_t, 32> key, std::span<const std::uint8_t, 12> nonce,
                  std::uint32_t counter_start, std::uint8_t* buf, std::size_t len) {
    std::size_t off = 0;
    std::uint32_t ctr = counter_start;
    while (off < len) {
        std::uint8_t block[64];
        chacha20_block(key.data(), nonce.data(), ctr, block);
        const std::size_t n = std::min<std::size_t>(len - off, 64);
        for (std::size_t i = 0; i < n; ++i) {
            buf[off + i] ^= block[i];
        }
        off += n;
        ++ctr;
    }
}

}  // namespace chat::crypto
