#pragma once

#include <string_view>

namespace chat {

/// Sikker-TCP INVITE skiller seg fra oblig 1 «lukket UDP»-rom ved denne markøren i PAYLOAD.
[[nodiscard]] inline bool is_secure_tcp_invite_payload(std::string_view payload) {
    return payload.find("secure_tcp=1") != std::string_view::npos;
}

}  // namespace chat
