// SHA-256 / HMAC-SHA256 for secure_message_crypto.
// Ingen socket- eller applikasjonsprotokoll her.
#include "chat/crypto/sha256_hmac.hpp"

#include <cstring>
#include <vector>

namespace chat::crypto {

namespace {

inline std::uint32_t load32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

inline void store32_be(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

static std::uint32_t rotr32(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t kSha256Init[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                          0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

void sha256_compress(std::uint32_t state[8], const std::uint8_t block[64]) {
    static const std::uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load32_be(block + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + k[i] + w[i];
        const std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t0 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t0 + t1;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

void sha256_hash(std::span<const std::uint8_t> data, std::span<std::uint8_t, 32> out_digest) {
    std::uint32_t st[8];
    std::memcpy(st, kSha256Init, sizeof(st));
    const std::uint64_t bitlen = static_cast<std::uint64_t>(data.size()) * 8ULL;
    std::size_t pos = 0;
    while (pos + 64 <= data.size()) {
        sha256_compress(st, data.data() + pos);
        pos += 64;
    }
    std::uint8_t block[64]{};
    const std::size_t rem = data.size() - pos;
    std::memcpy(block, data.data() + pos, rem);
    block[rem] = 0x80;
    if (rem < 56) {
        std::memset(block + rem + 1, 0, 55 - rem);
        for (int j = 0; j < 8; ++j) {
            block[56 + j] = static_cast<std::uint8_t>(bitlen >> (56 - 8 * j));
        }
        sha256_compress(st, block);
    } else {
        std::memset(block + rem + 1, 0, 63 - rem);
        sha256_compress(st, block);
        std::memset(block, 0, 56);
        for (int j = 0; j < 8; ++j) {
            block[56 + j] = static_cast<std::uint8_t>(bitlen >> (56 - 8 * j));
        }
        sha256_compress(st, block);
    }
    for (int i = 0; i < 8; ++i) {
        store32_be(out_digest.data() + i * 4, st[i]);
    }
}

void hmac_sha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message,
                 std::span<std::uint8_t, 32> out_mac) {
    std::uint8_t k[64]{};
    if (key.size() > 64) {
        sha256_hash(key, std::span<std::uint8_t, 32>{k, 32});
    } else {
        std::memcpy(k, key.data(), key.size());
    }
    std::uint8_t ipad[64];
    std::uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    std::vector<std::uint8_t> inner;
    inner.insert(inner.end(), ipad, ipad + 64);
    inner.insert(inner.end(), message.begin(), message.end());
    std::uint8_t inner_hash[32];
    sha256_hash(inner, std::span<std::uint8_t, 32>{inner_hash});
    std::vector<std::uint8_t> outer;
    outer.insert(outer.end(), opad, opad + 64);
    outer.insert(outer.end(), inner_hash, inner_hash + 32);
    sha256_hash(outer, out_mac);
}

}  // namespace chat::crypto
