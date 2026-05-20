// Brick 13 (part 2) — Tiny log-bucket latency histogram.
// We don't pull in HdrHistogram_c because it's a heavy dep; this is the same
// idea (logarithmic buckets, fixed memory, O(1) record) at ~5% accuracy --
// good enough for showing p50/p90/p99/p99.9 collapse before/after each brick.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace nanomatch {

class LatencyHistogram {
public:
    // 64 power-of-two buckets cover 1 ns .. ~18 exa-ns -- everything.
    static constexpr std::size_t kBuckets = 64;

    void record(std::uint64_t value_ns) noexcept {
        if (value_ns == 0) value_ns = 1;
        // index of the highest set bit -> log2 bucket
      #if defined(__GNUC__) || defined(__clang__)
        const int idx = 63 - __builtin_clzll(value_ns);
      #elif defined(_MSC_VER)
        unsigned long idx;
        _BitScanReverse64(&idx, value_ns);
      #else
        int idx = 0; auto v = value_ns; while (v >>= 1) ++idx;
      #endif
        ++counts_[idx];
        ++total_;
        if (value_ns > max_) max_ = value_ns;
        sum_ += value_ns;
    }

    std::uint64_t percentile(double p) const noexcept {
        if (total_ == 0) return 0;
        const std::uint64_t target = static_cast<std::uint64_t>(p * total_);
        std::uint64_t running = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            running += counts_[i];
            if (running >= target) {
                // mid-of-bucket estimate
                return (1ull << i) + (1ull << i) / 2;
            }
        }
        return max_;
    }

    void print(const char* label) const {
        std::printf("\n=== %s ===\n", label);
        std::printf("  count   : %llu\n", (unsigned long long)total_);
        if (total_ == 0) return;
        std::printf("  mean    : %.0f ns\n", (double)sum_ / (double)total_);
        std::printf("  p50     : %llu ns\n", (unsigned long long)percentile(0.50));
        std::printf("  p90     : %llu ns\n", (unsigned long long)percentile(0.90));
        std::printf("  p99     : %llu ns\n", (unsigned long long)percentile(0.99));
        std::printf("  p99.9   : %llu ns\n", (unsigned long long)percentile(0.999));
        std::printf("  max     : %llu ns\n", (unsigned long long)max_);
    }

    std::uint64_t count() const noexcept { return total_; }
    std::uint64_t sum()   const noexcept { return sum_; }

private:
    std::array<std::uint64_t, kBuckets> counts_{};
    std::uint64_t total_ = 0;
    std::uint64_t sum_   = 0;
    std::uint64_t max_   = 0;
};

} // namespace nanomatch
