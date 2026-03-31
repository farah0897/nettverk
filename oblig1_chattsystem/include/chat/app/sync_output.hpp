#pragma once

#include <mutex>

namespace chat {

/// Felles lås for meny og nettverkstråder — bruk rundt `std::cout`/`std::cerr`, aldri rundt blokkerende stdin.
extern std::mutex cout_mutex;

}  // namespace chat
