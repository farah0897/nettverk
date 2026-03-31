#pragma once

#include <cerrno>
#include <cstring>
#include <string>

namespace chat {

inline std::string errno_string(int e) {
    // `strerror` er trådsikker på glibc (TLS), og dette prosjektet er Linux-only.
    const char* s = std::strerror(e);
    return s ? std::string{s} : std::string{};
}

inline std::string errno_message(const char* what, int e = errno) {
    return std::string{what} + ": errno=" + std::to_string(e) + " (" + errno_string(e) + ")";
}

}  // namespace chat

