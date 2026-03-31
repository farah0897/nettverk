#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace chat {

/// Leser stdin/argumenter, parser kommandoer, og kaller tilbake (ingen nettlogikk).
class CliController {
public:
    using CommandHandler = std::function<void(std::string_view line)>;

    void set_handler(CommandHandler on_line);

    /// Blokkerer til én linje er lest (eller EOF). Returnerer false ved EOF.
    bool read_line(std::string& out);

    void print(std::string_view msg);

private:
    CommandHandler handler_;
};

}  // namespace chat
