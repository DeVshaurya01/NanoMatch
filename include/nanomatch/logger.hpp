// Brick 12 — Async trade logger.
// The matcher pushes Trade records into an SPSC ring. A second thread drains
// the ring and writes batched lines to disk. The matcher NEVER calls write/
// printf/cout. Backpressure: if the ring is full we drop the trade and bump
// `dropped_` -- exactly what real exchanges do (they replicate first).
#pragma once

#include "spsc_ring.hpp"
#include "types.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

namespace nanomatch {

class AsyncTradeLogger {
public:
    static constexpr std::size_t kRingCapacity = 1 << 20;  // ~1M slots

    explicit AsyncTradeLogger(const std::string& path);
    ~AsyncTradeLogger();

    AsyncTradeLogger(const AsyncTradeLogger&)            = delete;
    AsyncTradeLogger& operator=(const AsyncTradeLogger&) = delete;

    // Hot path: matcher calls this. Never blocks, never allocates.
    void log(const Trade& t) noexcept {
        if (!ring_.try_push(t)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    void run_();

    SPSCRing<Trade, kRingCapacity> ring_;
    std::atomic<std::uint64_t>     dropped_{0};
    std::atomic<bool>              stop_{false};
    std::FILE*                     fp_  = nullptr;
    std::thread                    thr_;
};

} // namespace nanomatch
