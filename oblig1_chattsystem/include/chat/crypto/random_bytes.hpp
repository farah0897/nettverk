#pragma once

/// @file random_bytes.hpp
/// Kryptografisk sikre tilfeldige byte fra operativsystemet.
///
/// Linux: `getrandom()` (foretrekker fremfor /dev/urandom-fil i denne koden).
/// Ved feil returnerer `fill_random` false; kallere må da avbryte kryptografiske operasjoner.
///
/// **Ikke brukt til:** ikke-kryptografiske PRNG-er (bruk `<random>` for det).

#include <cstddef>
#include <cstdint>
#include <span>

namespace chat::crypto {

/// Fyller hele `out` med kryptografisk tilfeldige byte. Tom span er OK (no-op, true).
[[nodiscard]] bool fill_random(std::span<std::uint8_t> out);

}  // namespace chat::crypto
