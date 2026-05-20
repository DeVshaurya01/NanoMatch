// Brick 3 — Naive baseline. std::map of std::list, no pool, no integer-ticks
// trickery beyond what types.hpp already mandates. Its purpose is to lose --
// it provides the "before" number every optimization gets compared against.
#pragma once

#include "types.hpp"
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace nanomatch {

class NaiveOrderBook {
public:
    using TradeSink = std::function<void(const Trade&)>;

    explicit NaiveOrderBook(TradeSink sink = {}) : sink_(std::move(sink)) {}

    void add_limit(const OrderEvent& ev);
    void cancel(OrderId id);
    void market(const OrderEvent& ev);

    // Diagnostics
    std::size_t bid_levels() const noexcept { return bids_.size(); }
    std::size_t ask_levels() const noexcept { return asks_.size(); }
    Price best_bid() const noexcept { return bids_.empty() ?  0 : bids_.begin()->first; }
    Price best_ask() const noexcept { return asks_.empty() ?  0 : asks_.begin()->first; }

private:
    // bids: descending; asks: ascending.
    using AskMap = std::map<Price, std::list<Order>>;
    using BidMap = std::map<Price, std::list<Order>, std::greater<Price>>;

    template <class BookSide, class Cmp>
    void match_against(OrderEvent& taker, BookSide& side, Cmp price_crosses);

    void emit(const Order& maker, const OrderEvent& taker, Price px, Quantity qty);

    BidMap bids_;
    AskMap asks_;
    // O(1) cancel lookup; stores list iterator + which map.
    struct Locator {
        Price price;
        Side  side;
        typename std::list<Order>::iterator it;
    };
    std::unordered_map<OrderId, Locator> index_;
    TradeSink sink_;
};

} // namespace nanomatch
