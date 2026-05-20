// Brick 11 — Single-producer / single-consumer wait-free ring buffer.
// Lamport's classic algorithm with Rigtorp's cache-of-the-opposite-cursor
// trick so producer & consumer don't bounce each other's cache lines.
//
// Every shared atomic and every per-side local cache live on their own cache
// line. False sharing here would silently 10x the cost of the queue.
#pragma once

#include "types.hpp"
#include <atomic>
#include <cstddef>
#include <new>

namespace nanomatch {

template <typename T, std::size_t Capacity>
class SPSCRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    static constexpr std::size_t kMask = Capacity - 1;

public:
    bool try_push(const T& v) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto next = w + 1;
        if (next - read_cache_ > Capacity) {
            read_cache_ = read_.load(std::memory_order_acquire);
            if (next - read_cache_ > Capacity) return false;          // full
        }
        slots_[w & kMask] = v;
        write_.store(next, std::memory_order_release);                // publish
        return true;
    }

    bool try_pop(T& out) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_cache_) {
            write_cache_ = write_.load(std::memory_order_acquire);
            if (r == write_cache_) return false;                      // empty
        }
        out = slots_[r & kMask];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    std::size_t size_approx() const noexcept {
        return write_.load(std::memory_order_acquire)
             - read_.load(std::memory_order_acquire);
    }

private:
    alignas(kCacheLine) std::atomic<std::uint64_t> write_{0};
    alignas(kCacheLine) std::uint64_t              read_cache_{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> read_{0};
    alignas(kCacheLine) std::uint64_t              write_cache_{0};
    alignas(kCacheLine) T                          slots_[Capacity];
};

} // namespace nanomatch
