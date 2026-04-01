#pragma once

#include <mutex>

namespace chat {

/// Lås for `cout`/`cerr` fra meny og nettverkstråder (ikke rundt blokkerende stdin).
extern std::mutex cout_mutex;

}  // namespace chat
