#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace chat {

/// Enkel stoppbar vent: tråder kan sove med timeout, men våkne raskt ved shutdown.
class InterruptibleSleep {
public:
    void request_stop() {
        stopped_.store(true);
        cv_.notify_all();
    }

    bool stopped() const { return stopped_.load(); }

    /// Returnerer false hvis stopp ble forespurt før timeout.
    template <class Rep, class Period>
    bool sleep_for(const std::chrono::duration<Rep, Period>& d) {
        std::unique_lock lock{mu_};
        if (stopped_.load()) {
            return false;
        }
        cv_.wait_for(lock, d, [&] { return stopped_.load(); });
        return !stopped_.load();
    }

private:
    std::atomic<bool> stopped_{false};
    std::mutex mu_;
    std::condition_variable cv_;
};

}  // namespace chat

