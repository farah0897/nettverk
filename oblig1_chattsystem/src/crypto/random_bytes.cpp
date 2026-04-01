// Tilfeldige bytes (getrandom); brukt til nonce/nøkkel.
#include "chat/crypto/random_bytes.hpp"

#include <cerrno>
#include <sys/random.h>

#include <cstring>

namespace chat::crypto {

bool fill_random(std::span<std::uint8_t> out) {
    if (out.empty()) {
        return true;
    }
    std::size_t got = 0;
    while (got < out.size()) {
        const ssize_t r = ::getrandom(out.data() + got, out.size() - got, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (r == 0) {
            return false;
        }
        got += static_cast<std::size_t>(r);
    }
    return true;
}

}  // namespace chat::crypto
