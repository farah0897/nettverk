#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include <poll.h>

namespace chat {

/// Samler `poll()` på stdin, UDP-socket(er) og evt. `timerfd` for periodiske oppgaver.
class PollMultiplexer {
public:
    void add_fd(int fd, short events);
    void remove_fd(int fd);

    /// Negativ timeout = uendelig. Returnerer antall klare hendelser, 0 ved timeout, -1 ved feil.
    int wait(std::chrono::milliseconds timeout);

    std::vector<pollfd>& fds();

private:
    std::vector<pollfd> fds_;
};

}  // namespace chat
