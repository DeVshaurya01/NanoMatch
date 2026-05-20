// Brick 5 — Unit tests. Self-contained mini-framework (no gtest dep) so the
// tests run anywhere CMake + a C++20 compiler do. Each test asserts a single
// canonical matching-engine invariant from the blueprint.
#include "nanomatch/order_book.hpp"
#include "nanomatch/order_book_naive.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace nanomatch;

static int g_failed = 0;

#define CHECK(cond) do {                                                       \
    if (!(cond)) {                                                             \
        std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        ++g_failed;                                                            \
    }                                                                          \
} while (0)

namespace {

OrderEvent mk(EventKind k, OrderId id, Side s, Price px, Quantity q,
              Timestamp ts = 0) {
    OrderEvent e{}; e.kind = k; e.id = id; e.side = s; e.price = px;
    e.qty = q; e.ts = ts; e.type = OrdType::Limit; return e;
}

void run_test(const char* name, void (*fn)()) {
    const int before = g_failed;
    std::printf("[ %-50s ]", name);
    fn();
    std::printf(" %s\n", g_failed == before ? "ok" : "FAILED");
}

// ---- the actual tests -----------------------------------------------------

void t_rest_no_cross() {
    OrderBook<200'000, 100'000> b;
    b.add_limit(mk(EventKind::Add, 1, Side::Buy, 100, 10));
    CHECK(b.best_bid_price() == 100);
    CHECK(b.best_ask_price() == 0);
}

void t_full_cross() {
    int trades = 0; Quantity total = 0;
    OrderBook<200'000, 100'000> b([&](const Trade& t){ ++trades; total += t.qty; });
    b.add_limit(mk(EventKind::Add, 1, Side::Sell, 100, 10));
    b.add_limit(mk(EventKind::Add, 2, Side::Buy,  100, 10));
    CHECK(trades == 1);
    CHECK(total == 10);
}

void t_partial_fill() {
    Quantity total = 0;
    OrderBook<200'000, 100'000> b([&](const Trade& t){ total += t.qty; });
    b.add_limit(mk(EventKind::Add, 1, Side::Sell, 100, 10));
    b.add_limit(mk(EventKind::Add, 2, Side::Buy,  100, 4));
    CHECK(total == 4);
    // Remaining 6 at 100 should still match a second buy at 100.
    b.add_limit(mk(EventKind::Add, 3, Side::Buy, 100, 6));
    CHECK(total == 10);
}

void t_price_time_priority() {
    std::vector<OrderId> makers;
    OrderBook<200'000, 100'000> b(
        [&](const Trade& t){ makers.push_back(t.maker_id); });
    b.add_limit(mk(EventKind::Add, 1, Side::Sell, 100, 5, /*ts*/1));
    b.add_limit(mk(EventKind::Add, 2, Side::Sell, 100, 5, /*ts*/2));
    b.add_limit(mk(EventKind::Add, 3, Side::Buy,  100, 7));
    CHECK(makers.size() == 2);
    CHECK(makers[0] == 1);
    CHECK(makers[1] == 2);
}

void t_cancel_non_top() {
    OrderBook<200'000, 100'000> b;
    b.add_limit(mk(EventKind::Add, 1, Side::Buy, 99,  3));
    b.add_limit(mk(EventKind::Add, 2, Side::Buy, 100, 4));
    b.cancel(1);
    CHECK(b.best_bid_price() == 100);
}

void t_cancel_best_walks_bbo() {
    OrderBook<200'000, 100'000> b;
    b.add_limit(mk(EventKind::Add, 1, Side::Buy, 99,  3));
    b.add_limit(mk(EventKind::Add, 2, Side::Buy, 100, 4));
    b.cancel(2);
    CHECK(b.best_bid_price() == 99);
}

void t_market_empty_book() {
    int trades = 0;
    OrderBook<200'000, 100'000> b([&](const Trade&){ ++trades; });
    OrderEvent ev = mk(EventKind::Add, 1, Side::Buy, 0, 10);
    ev.kind = EventKind::MarketOrder;
    b.market(ev);
    CHECK(trades == 0);                       // nothing to fill against
    CHECK(b.best_bid_price() == 0);           // market orders don't rest
}

void t_naive_baseline_smoke() {
    int trades = 0;
    NaiveOrderBook b([&](const Trade&){ ++trades; });
    b.add_limit(mk(EventKind::Add, 1, Side::Sell, 100, 10));
    b.add_limit(mk(EventKind::Add, 2, Side::Buy,  100, 10));
    CHECK(trades == 1);
}

} // namespace

int main() {
    std::printf("NanoMatch tests\n---------------\n");
    run_test("rest with no cross",                   t_rest_no_cross);
    run_test("full cross emits one trade",           t_full_cross);
    run_test("partial fill leaves resting maker",    t_partial_fill);
    run_test("price-time priority on same level",    t_price_time_priority);
    run_test("cancel of non-top order",              t_cancel_non_top);
    run_test("cancel of best walks BBO",             t_cancel_best_walks_bbo);
    run_test("market order against empty book",      t_market_empty_book);
    run_test("naive baseline smoke",                 t_naive_baseline_smoke);
    std::printf("---\n%s (%d failed)\n",
                g_failed == 0 ? "PASS" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
