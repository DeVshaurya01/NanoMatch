// Brick 13 (part 1) — Nanosecond clocks.
// On x86 we expose an rdtsc helper for the very hot path; everywhere else we
// fall back to steady_clock. Calibration happens once at startup against
// steady_clock so we can convert cycles -> ns.
#pragma once

#include <chrono>
#include <cstdint>

#if defined(_MSC_VER)
  #include <intrin.h>
#elif defined(__x86_64__) || defined(_M_X64)
  #include <x86intrin.h>
#endif

namespace nanomatch {

inline std::uint64_t now_ns() noexcept {
    using clk = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            clk::now().time_since_epoch()).count());
}

#if defined(__x86_64__) || defined(_M_X64)
inline std::uint64_t rdtsc_start() noexcept {
  #if defined(_MSC_VER)
    _mm_lfence();
    return __rdtsc();
  #else
    _mm_lfence();
    return __rdtsc();
  #endif
}

inline std::uint64_t rdtsc_end() noexcept {
    unsigned aux;
  #if defined(_MSC_VER)
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
  #else
    const std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
  #endif
}
#else
inline std::uint64_t rdtsc_start() noexcept { return now_ns(); }
inline std::uint64_t rdtsc_end()   noexcept { return now_ns(); }
#endif

// One-shot calibration: spin for ~10 ms and derive cycles-per-ns.
inline double calibrate_tsc_ghz() noexcept {
    const auto t0_ns  = now_ns();
    const auto c0     = rdtsc_start();
    while (now_ns() - t0_ns < 10'000'000ull) { /* spin ~10 ms */ }
    const auto c1     = rdtsc_end();
    const auto t1_ns  = now_ns();
    const auto dc     = static_cast<double>(c1 - c0);
    const auto dns    = static_cast<double>(t1_ns - t0_ns);
    return dc / dns; // cycles per ns
}

} // namespace nanomatch
