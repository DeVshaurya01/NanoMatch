// End-to-end demo binary. Wires every brick together:
//   ITCH file (mmap, Brick 10) -> parser -> matcher (Brick 7..9) ->
//   trade sink -> trades.csv
//
// Sink selection at compile time:
//   - Default: synchronous FILE* sink (no std::thread)
//   - -DNANOMATCH_USE_ASYNC_LOGGER=ON in CMake: SPSC ring + dedicated logger
//     thread (Brick 12). The lock-free path; disabled by default because
//     std::thread in an unsigned MinGW binary trips Windows SmartScreen.
//     Enable it once you have the binary signed or you're running in WSL2.
#include "nanomatch/file_map.hpp"
#include "nanomatch/histogram.hpp"
#include "nanomatch/itch_parser.hpp"
#include "nanomatch/order_book.hpp"
#include "nanomatch/timing.hpp"

#if defined(NANOMATCH_USE_ASYNC_LOGGER) && NANOMATCH_USE_ASYNC_LOGGER
  #include "nanomatch/logger.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>

using namespace nanomatch;

namespace {

// --- Sink abstraction. Sync version writes directly; async version pushes
// into an SPSC ring drained by a dedicated logger thread. Both expose the
// same operator()(const Trade&) so the matcher doesn't care which is in use.

#if defined(NANOMATCH_USE_ASYNC_LOGGER) && NANOMATCH_USE_ASYNC_LOGGER

struct Sink {
    AsyncTradeLogger logger;
    std::uint64_t&   count_ref;
    Sink(const char* path, std::uint64_t& cnt) : logger(path), count_ref(cnt) {}
    void operator()(const Trade& t) noexcept { ++count_ref; logger.log(t); }
    std::uint64_t dropped() const noexcept { return logger.dropped(); }
    bool ok() const noexcept { return true; }
};

#else

struct Sink {
    std::FILE*     fp;
    std::uint64_t& count_ref;
    Sink(const char* path, std::uint64_t& cnt) : count_ref(cnt) {
        fp = std::fopen(path, "wb");
        if (fp) {
            std::setvbuf(fp, nullptr, _IOFBF, 1 << 16);
            std::fprintf(fp, "ts,maker_id,taker_id,price,qty,taker_side\n");
        }
    }
    ~Sink() { if (fp) std::fclose(fp); }
    void operator()(const Trade& t) noexcept {
        ++count_ref;
        if (fp) {
            std::fprintf(fp, "%llu,%llu,%llu,%lld,%u,%c\n",
                (unsigned long long)t.ts,
                (unsigned long long)t.maker_id,
                (unsigned long long)t.taker_id,
                (long long)t.price, (unsigned)t.qty,
                static_cast<char>(t.taker_side));
        }
    }
    std::uint64_t dropped() const noexcept { return 0; }
    bool ok() const noexcept { return fp != nullptr; }
};

#endif

void run_synthetic(std::uint64_t n) {
    std::uint64_t trade_count = 0;
    Sink sink("trades.csv", trade_count);
    if (!sink.ok()) {
        std::fprintf(stderr, "could not open trades.csv\n");
        return;
    }
    OrderBook<200'000, 2'000'000> book(std::ref(sink));

    LatencyHistogram hist;
    std::mt19937_64 rng(42);
    const Price mid = 100'0000;

    // Calibrate TSC for sub-100ns timing (Windows steady_clock is 100ns).
    const double tsc_ghz = calibrate_tsc_ghz();

    const auto wall0 = now_ns();
    for (std::uint64_t i = 0; i < n; ++i) {
        OrderEvent ev{};
        ev.id    = i + 1;
        ev.ts    = i;
        ev.type  = OrdType::Limit;
        ev.qty   = 1 + (rng() % 500);
        ev.price = mid + (std::int64_t)(rng() % 200) - 100;
        ev.side  = (rng() & 1) ? Side::Buy : Side::Sell;
        ev.kind  = EventKind::Add;

        const auto c0 = rdtsc_start();
        book.add_limit(ev);
        const auto c1 = rdtsc_end();
        const auto ns = static_cast<std::uint64_t>(
            static_cast<double>(c1 - c0) / tsc_ghz);
        hist.record(ns);
    }
    const auto wall1 = now_ns();
    const double secs = (wall1 - wall0) / 1e9;

    std::printf("synthetic: %llu orders in %.3fs -> %.2f Morders/s\n",
                (unsigned long long)n, secs, (n / secs) / 1e6);
    std::printf("TSC: %.3f GHz\n", tsc_ghz);
    hist.print("match latency (synthetic, rdtsc)");
    std::printf("trades emitted: %llu\n", (unsigned long long)trade_count);
    std::printf("dropped (async only): %llu\n",
                (unsigned long long)sink.dropped());
}

void run_itch(const std::string& path) {
    MappedFile mf(path);
    if (!mf.ok()) {
        std::fprintf(stderr, "could not map %s\n", path.c_str());
        std::exit(1);
    }
    std::printf("mapped %s : %zu bytes\n", path.c_str(), mf.size());

    std::uint64_t trade_count = 0;
    Sink sink("trades.csv", trade_count);
    if (!sink.ok()) {
        std::fprintf(stderr, "could not open trades.csv\n");
        return;
    }
    OrderBook<> book(std::ref(sink));

    const auto wall0 = now_ns();
    const ItchStats st = parse_itch_buffer(
        mf.data(), mf.size(),
        [&](const OrderEvent& ev) {
            if (ev.kind == EventKind::Add)         book.add_limit(ev);
            else if (ev.kind == EventKind::Cancel) book.cancel(ev.id);
        });
    const auto wall1 = now_ns();
    const double secs = (wall1 - wall0) / 1e9;

    std::printf("ITCH parsed: msgs=%llu adds=%llu execs=%llu cancels=%llu "
                "deletes=%llu replaces=%llu skipped=%llu\n",
                (unsigned long long)st.messages, (unsigned long long)st.adds,
                (unsigned long long)st.execs,    (unsigned long long)st.cancels,
                (unsigned long long)st.deletes,  (unsigned long long)st.replaces,
                (unsigned long long)st.skipped);
    std::printf("elapsed: %.3fs -> %.2f Mmsg/s\n",
                secs, (st.messages / secs) / 1e6);
    std::printf("trades emitted: %llu\n", (unsigned long long)trade_count);
    std::printf("dropped (async only): %llu\n",
                (unsigned long long)sink.dropped());
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#if defined(NANOMATCH_USE_ASYNC_LOGGER) && NANOMATCH_USE_ASYNC_LOGGER
    std::printf("[build] async logger ENABLED (Brick 12: SPSC ring + thread)\n");
#else
    std::printf("[build] async logger disabled (synchronous FILE* sink)\n");
#endif
    if (argc >= 3 && std::strcmp(argv[1], "--synthetic") == 0) {
        run_synthetic(std::strtoull(argv[2], nullptr, 10));
        return 0;
    }
    if (argc >= 2) {
        run_itch(argv[1]);
        return 0;
    }
    std::printf("usage:\n"
                "  %s <itch_file>\n"
                "  %s --synthetic <N_orders>\n", argv[0], argv[0]);
    run_synthetic(1'000'000);
    return 0;
}
