#include "nanomatch/logger.hpp"

#include <chrono>

namespace nanomatch {

AsyncTradeLogger::AsyncTradeLogger(const std::string& path) {
    fp_ = std::fopen(path.c_str(), "wb");
    if (fp_) {
        std::setvbuf(fp_, nullptr, _IOFBF, 1 << 16);
        std::fprintf(fp_, "ts,maker_id,taker_id,price,qty,taker_side\n");
    }
    thr_ = std::thread([this]{ run_(); });
}

AsyncTradeLogger::~AsyncTradeLogger() {
    stop_.store(true, std::memory_order_release);
    if (thr_.joinable()) thr_.join();
    if (fp_) { std::fflush(fp_); std::fclose(fp_); fp_ = nullptr; }
}

void AsyncTradeLogger::run_() {
    Trade t{};
    while (!stop_.load(std::memory_order_acquire)) {
        bool any = false;
        // Drain in a tight batch -- amortize barrier cost.
        for (int i = 0; i < 1024 && ring_.try_pop(t); ++i) {
            any = true;
            if (fp_) {
                std::fprintf(fp_, "%llu,%llu,%llu,%lld,%u,%c\n",
                    (unsigned long long)t.ts,
                    (unsigned long long)t.maker_id,
                    (unsigned long long)t.taker_id,
                    (long long)t.price,
                    (unsigned)t.qty,
                    static_cast<char>(t.taker_side));
            }
        }
        if (!any) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    // Drain anything left on shutdown.
    while (ring_.try_pop(t)) {
        if (fp_) {
            std::fprintf(fp_, "%llu,%llu,%llu,%lld,%u,%c\n",
                (unsigned long long)t.ts,
                (unsigned long long)t.maker_id,
                (unsigned long long)t.taker_id,
                (long long)t.price,
                (unsigned)t.qty,
                static_cast<char>(t.taker_side));
        }
    }
}

} // namespace nanomatch
