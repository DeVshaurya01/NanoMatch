#include "nanomatch/order_book_naive.hpp"
#include <algorithm>

namespace nanomatch {

void NaiveOrderBook::emit(const Order& maker, const OrderEvent& taker,
                          Price px, Quantity qty) {
    if (sink_) {
        Trade t{};
        t.maker_id   = maker.id;
        t.taker_id   = taker.id;
        t.price      = px;
        t.qty        = qty;
        t.ts         = taker.ts;
        t.taker_side = taker.side;
        sink_(t);
    }
}

template <class BookSide, class Cmp>
void NaiveOrderBook::match_against(OrderEvent& taker, BookSide& side, Cmp crosses) {
    while (taker.qty > 0 && !side.empty()) {
        auto best = side.begin();
        if (taker.kind != EventKind::MarketOrder && !crosses(best->first, taker.price))
            break;
        auto& lst = best->second;
        while (taker.qty > 0 && !lst.empty()) {
            Order& maker = lst.front();
            const Quantity fill = std::min(taker.qty, maker.qty);
            emit(maker, taker, best->first, fill);
            maker.qty -= fill;
            taker.qty -= fill;
            if (maker.qty == 0) {
                index_.erase(maker.id);
                lst.pop_front();
            }
        }
        if (lst.empty()) side.erase(best);
    }
}

void NaiveOrderBook::add_limit(const OrderEvent& ev_in) {
    OrderEvent ev = ev_in;
    if (ev.side == Side::Buy) {
        match_against(ev, asks_, [](Price ask, Price bid){ return ask <= bid; });
    } else {
        match_against(ev, bids_, [](Price bid, Price ask){ return bid >= ask; });
    }
    if (ev.qty == 0) return;
    Order o{};
    o.id = ev.id; o.price = ev.price; o.qty = ev.qty; o.orig_qty = ev_in.qty;
    o.ts = ev.ts; o.side = ev.side; o.type = ev.type;
    if (ev.side == Side::Buy) {
        auto& lst = bids_[ev.price];
        lst.push_back(o);
        index_.emplace(ev.id, Locator{ev.price, Side::Buy, std::prev(lst.end())});
    } else {
        auto& lst = asks_[ev.price];
        lst.push_back(o);
        index_.emplace(ev.id, Locator{ev.price, Side::Sell, std::prev(lst.end())});
    }
}

void NaiveOrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    const auto& loc = it->second;
    if (loc.side == Side::Buy) {
        auto map_it = bids_.find(loc.price);
        if (map_it != bids_.end()) {
            map_it->second.erase(loc.it);
            if (map_it->second.empty()) bids_.erase(map_it);
        }
    } else {
        auto map_it = asks_.find(loc.price);
        if (map_it != asks_.end()) {
            map_it->second.erase(loc.it);
            if (map_it->second.empty()) asks_.erase(map_it);
        }
    }
    index_.erase(it);
}

void NaiveOrderBook::market(const OrderEvent& ev_in) {
    OrderEvent ev = ev_in;
    ev.kind = EventKind::MarketOrder;
    if (ev.side == Side::Buy) {
        match_against(ev, asks_, [](Price, Price){ return true; });
    } else {
        match_against(ev, bids_, [](Price, Price){ return true; });
    }
}

} // namespace nanomatch
