// Brick 14 — Headline benchmark: naive (std::map+list) vs the optimized
// bucket book + intrusive list + pool. This is the "before/after" number
// that goes in the README.
//
// Timing: we use rdtsc (Time Stamp Counter, ~1 cycle ≈ 0.25 ns) for per-op
// latency. Windows' steady_clock has 100 ns granularity which would otherwise
// pin p50 to a single bucket. We calibrate TSC->ns once at startup via a
// brief steady_clock spin (see timing.hpp) and convert cycles back to ns
// before recording into the histogram.
//
// Hygiene:
//   - identical input event stream replayed against both books
//   - first 5% of samples discarded as warm-up
//   - -O3 -DNDEBUG enforced via CMakeLists.txt
//   - per-op timing pinned to a single core via PROCESS_AFFINITY hint
//     (handled by run_bench.sh on Linux; on Windows we accept slight noise)
#include "nanomatch/histogram.hpp"
#include "nanomatch/order_book.hpp"
#include "nanomatch/order_book_naive.hpp"
#include "nanomatch/timing.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace nanomatch;

namespace {

std::vector<OrderEvent> make_stream(std::uint64_t n, std::uint64_t seed = 42) {
    std::vector<OrderEvent> v; v.reserve(n);
    std::mt19937_64 rng(seed);
    const Price mid = 100'0000;
    for (std::uint64_t i = 0; i < n; ++i) {
        OrderEvent e{};
        e.id    = i + 1;
        e.ts    = i;
        e.type  = OrdType::Limit;
        e.qty   = 1 + (rng() % 500);
        e.price = mid + (std::int64_t)(rng() % 200) - 100;
        e.side  = (rng() & 1) ? Side::Buy : Side::Sell;
        // ~10% cancels of a previously-seen id
        if (i > 1000 && (rng() % 10) == 0) {
            e.kind = EventKind::Cancel;
            e.id   = 1 + (rng() % i);
        } else {
            e.kind = EventKind::Add;
        }
        v.push_back(e);
    }
    return v;
}

template <class Book>
void run(const char* label, Book& book,
         const std::vector<OrderEvent>& stream,
         double tsc_ghz) {
    LatencyHistogram hist;
    const auto warm = stream.size() / 20;        // 5% warm-up
    const auto wall0 = now_ns();
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const auto& ev = stream[i];
        const auto c0 = rdtsc_start();
        if (ev.kind == EventKind::Add)         book.add_limit(ev);
        else if (ev.kind == EventKind::Cancel) book.cancel(ev.id);
        const auto c1 = rdtsc_end();
        if (i >= warm) {
            // cycles -> ns. tsc_ghz is cycles/ns; divide.
            const auto ns = static_cast<std::uint64_t>(
                static_cast<double>(c1 - c0) / tsc_ghz);
            hist.record(ns);
        }
    }
    const auto wall1 = now_ns();
    const double secs = (wall1 - wall0) / 1e9;
    std::printf("\n[%s]\n", label);
    std::printf("  throughput : %.2f Mops/s (%.3fs over %zu events)\n",
                stream.size() / secs / 1e6, secs, stream.size());
    hist.print(label);
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t N = (argc >= 2) ? std::strtoull(argv[1], nullptr, 10)
                                        : 2'000'000ull;

    // Calibrate TSC against steady_clock. Takes ~10 ms.
    std::printf("calibrating TSC against steady_clock...\n");
    const double tsc_ghz = calibrate_tsc_ghz();
    std::printf("TSC frequency: %.3f GHz (%.3f cycles/ns)\n",
                tsc_ghz, tsc_ghz);

    std::printf("NanoMatch benchmark: %llu events\n",
                (unsigned long long)N);

    auto stream = make_stream(N);

    {
        NaiveOrderBook naive;
        run("NAIVE  std::map + std::list (Brick 3)", naive, stream, tsc_ghz);
    }
    {
        OrderBook<200'000, 4'000'000> fast;
        run("FAST   bucket book + pool + intrusive list (Bricks 7-9)",
            fast, stream, tsc_ghz);
    }
    return 0;
}
