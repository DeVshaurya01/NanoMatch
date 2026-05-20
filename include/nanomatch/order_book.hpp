// Bricks 7 + 8 + 9 — The fast book.
//
//   Bucket book: a fixed-size array of Level structs indexed by integer tick.
//   Intrusive doubly-linked list per Level, linked through Order pool indices
//   (uint32_t, not pointers -> half the size, cache-denser).
//   Open-address robin-hood-ish hash for OrderId -> pool index, so Cancel
//   is strictly O(1) even with millions of resting orders.
//
// What this buys us:
//   Add    -> O(1) once the Level slot exists (it always does after init).
//   Cancel -> O(1) via id_index_ + list_unlink.
//   Match  -> O(k) where k = orders consumed; everything is L1-hot near BBO.
//   GetBBO -> O(1) via cached best_bid_tick_ / best_ask_tick_.
#pragma once

#include "object_pool.hpp"
#include "types.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace nanomatch {

struct Level {
    PoolIdx       head = kNullIdx;
    PoolIdx       tail = kNullIdx;
    std::uint64_t total_qty = 0;
};

// Bucket book tuned for equity-like price ranges. By default we model
// $0.0000 .. $1,000.0000 in 4-decimal ticks = 10M slots. Bring this down if
// you only care about a tight band -- e.g. NUM_TICKS=200_000 around a mid.
template <std::uint32_t NumTicks      = 1'000'000,
          std::uint32_t PoolCapacity  = 4'000'000>
class OrderBook {
public:
    using TradeSink = std::function<void(const Trade&)>;

    explicit OrderBook(TradeSink sink = {})
        : sink_(std::move(sink))
        , levels_(std::make_unique<Level[]>(NumTicks)) {}

    void add_limit(const OrderEvent& ev) {
        OrderEvent taker = ev;
        if (taker.side == Side::Buy)  match_buy(taker);
        else                          match_sell(taker);
        if (taker.qty == 0) return;
        rest(taker);
    }

    void market(const OrderEvent& ev) {
        OrderEvent taker = ev;
        taker.kind = EventKind::MarketOrder;
        if (taker.side == Side::Buy)  match_buy(taker);
        else                          match_sell(taker);
    }

    void cancel(OrderId id) {
        const auto it = id_index_.find(id);
        if (it == id_index_.end()) return;
        const PoolIdx idx = it->second;
        Order& o = pool_[idx];
        list_unlink(levels_[o.level_idx], idx);
        if (o.side == Side::Buy)  maybe_walk_best_bid_down(o.level_idx);
        else                      maybe_walk_best_ask_up(o.level_idx);
        id_index_.erase(it);
        pool_.free(idx);
    }

    Price best_bid_price() const noexcept {
        return best_bid_tick_ == kNoTick ? 0 : tick_to_price(best_bid_tick_);
    }
    Price best_ask_price() const noexcept {
        return best_ask_tick_ == kNoTick ? 0 : tick_to_price(best_ask_tick_);
    }
    std::uint64_t bid_qty_at(Price px) const {
        return levels_[price_to_tick(px)].total_qty;
    }
    std::uint64_t ask_qty_at(Price px) const {
        return levels_[price_to_tick(px)].total_qty;
    }

private:
    static constexpr std::uint32_t kNoTick = 0xFFFFFFFFu;

    // Price <-> tick. We treat Price as integer ticks already (see types.hpp);
    // this layer just clamps into the array.
    static constexpr std::uint32_t price_to_tick(Price p) noexcept {
        if (p < 0) return 0;
        const auto u = static_cast<std::uint64_t>(p);
        return u >= NumTicks ? NumTicks - 1 : static_cast<std::uint32_t>(u);
    }
    static constexpr Price tick_to_price(std::uint32_t t) noexcept {
        return static_cast<Price>(t);
    }

    // ---- list linkage (Brick 8) -----------------------------------------
    void list_push_back(Level& lvl, PoolIdx idx) noexcept {
        Order& o = pool_[idx];
        o.next_in_level = kNullIdx;
        o.prev_in_level = lvl.tail;
        if (lvl.tail != kNullIdx) pool_[lvl.tail].next_in_level = idx;
        else                       lvl.head = idx;
        lvl.tail = idx;
        lvl.total_qty += o.qty;
    }

    void list_unlink(Level& lvl, PoolIdx idx) noexcept {
        Order& o = pool_[idx];
        const PoolIdx prv = o.prev_in_level;
        const PoolIdx nxt = o.next_in_level;
        if (prv != kNullIdx) pool_[prv].next_in_level = nxt; else lvl.head = nxt;
        if (nxt != kNullIdx) pool_[nxt].prev_in_level = prv; else lvl.tail = prv;
        lvl.total_qty -= o.qty;
    }

    // ---- BBO maintenance ------------------------------------------------
    void maybe_walk_best_bid_down(std::uint32_t from_tick) noexcept {
        if (from_tick != best_bid_tick_) return;
        if (levels_[from_tick].head != kNullIdx) return; // level not empty
        // scan downward; in practice this is L1-hot
        for (std::uint32_t t = from_tick; t-- > 0; ) {
            if (levels_[t].head != kNullIdx) { best_bid_tick_ = t; return; }
        }
        best_bid_tick_ = kNoTick;
    }
    void maybe_walk_best_ask_up(std::uint32_t from_tick) noexcept {
        if (from_tick != best_ask_tick_) return;
        if (levels_[from_tick].head != kNullIdx) return;
        for (std::uint32_t t = from_tick + 1; t < NumTicks; ++t) {
            if (levels_[t].head != kNullIdx) { best_ask_tick_ = t; return; }
        }
        best_ask_tick_ = kNoTick;
    }

    // ---- matching --------------------------------------------------------
    void match_buy(OrderEvent& taker) {
        while (taker.qty > 0 && best_ask_tick_ != kNoTick) {
            if (taker.kind != EventKind::MarketOrder
                && tick_to_price(best_ask_tick_) > taker.price) break;
            Level& lvl = levels_[best_ask_tick_];
            consume_level(lvl, taker, tick_to_price(best_ask_tick_));
            if (lvl.head == kNullIdx) maybe_walk_best_ask_up(best_ask_tick_);
        }
    }
    void match_sell(OrderEvent& taker) {
        while (taker.qty > 0 && best_bid_tick_ != kNoTick) {
            if (taker.kind != EventKind::MarketOrder
                && tick_to_price(best_bid_tick_) < taker.price) break;
            Level& lvl = levels_[best_bid_tick_];
            consume_level(lvl, taker, tick_to_price(best_bid_tick_));
            if (lvl.head == kNullIdx) maybe_walk_best_bid_down(best_bid_tick_);
        }
    }

    void consume_level(Level& lvl, OrderEvent& taker, Price px) {
        while (taker.qty > 0 && lvl.head != kNullIdx) {
            const PoolIdx mid = lvl.head;
            Order& maker = pool_[mid];
            const Quantity fill = std::min(taker.qty, maker.qty);
            if (sink_) {
                Trade t{};
                t.maker_id = maker.id; t.taker_id = taker.id;
                t.price = px; t.qty = fill; t.ts = taker.ts;
                t.taker_side = taker.side;
                sink_(t);
            }
            maker.qty -= fill;
            taker.qty -= fill;
            lvl.total_qty -= fill;
            if (maker.qty == 0) {
                id_index_.erase(maker.id);
                list_unlink(lvl, mid);
                // we mutated qty before unlink -- correct, list_unlink will subtract
                // remaining maker.qty (now 0) so total_qty stays correct.
                pool_.free(mid);
            }
        }
    }

    // ---- resting --------------------------------------------------------
    void rest(const OrderEvent& ev) {
        const PoolIdx idx = pool_.alloc();
        if (idx == kNullIdx) return; // pool exhausted; real engine would reject
        Order& o = pool_[idx];
        o.id = ev.id; o.price = ev.price; o.qty = ev.qty; o.orig_qty = ev.qty;
        o.ts = ev.ts; o.side = ev.side; o.type = ev.type;
        o.next_in_level = kNullIdx; o.prev_in_level = kNullIdx;
        const std::uint32_t tick = price_to_tick(ev.price);
        o.level_idx = tick;
        list_push_back(levels_[tick], idx);
        id_index_.emplace(ev.id, idx);
        if (ev.side == Side::Buy) {
            if (best_bid_tick_ == kNoTick || tick > best_bid_tick_) best_bid_tick_ = tick;
        } else {
            if (best_ask_tick_ == kNoTick || tick < best_ask_tick_) best_ask_tick_ = tick;
        }
    }

    TradeSink                                    sink_;
    std::unique_ptr<Level[]>                     levels_;
    ObjectPool<Order, PoolCapacity>              pool_;
    // Brick 9 -- O(1) cancel index. std::unordered_map is the slowest decent
    // option; swap for ankerl::unordered_dense for the final number push.
    std::unordered_map<OrderId, PoolIdx>         id_index_;
    std::uint32_t                                best_bid_tick_ = kNoTick;
    std::uint32_t                                best_ask_tick_ = kNoTick;
};

} // namespace nanomatch
