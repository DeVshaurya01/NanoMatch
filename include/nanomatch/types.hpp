// Brick 2 — Core integer-tick types shared across the engine.
// Floats are deliberately absent: ITCH encodes price as int32 with 4 implied
// decimals, and we propagate that integer all the way through matching.
#pragma once

#include <cstdint>
#include <new>

namespace nanomatch {

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

using OrderId   = std::uint64_t;
using Price     = std::int64_t;     // ticks (integer, 4 implied decimals for ITCH)
using Quantity  = std::uint32_t;
using Timestamp = std::uint64_t;    // ns since some epoch (engine ingest time)
using PoolIdx   = std::uint32_t;

inline constexpr PoolIdx kNullIdx = 0xFFFFFFFFu;

enum class Side    : std::uint8_t { Buy = 'B', Sell = 'S' };
enum class OrdType : std::uint8_t { Limit, Market, IOC, FOK };

// Hot-path order record. Kept <= 64B so every order touches one cache line.
struct Order {
    OrderId   id;             // 8
    Price     price;          // 8   (tick units)
    Quantity  qty;            // 4   remaining
    Quantity  orig_qty;       // 4
    Timestamp ts;             // 8
    PoolIdx   next_in_level;  // 4   intrusive list link (pool index, not ptr)
    PoolIdx   prev_in_level;  // 4
    std::uint32_t level_idx;  // 4   back-pointer into level array
    Side      side;           // 1
    OrdType   type;           // 1
    std::uint8_t _pad[2];     // 2   round to 48 (we have spare)
};
static_assert(sizeof(Order) <= 64, "Order must fit in a single cache line");

struct Trade {
    OrderId   maker_id;
    OrderId   taker_id;
    Price     price;
    Quantity  qty;
    Timestamp ts;
    Side      taker_side;
    std::uint8_t _pad[7];
};
static_assert(sizeof(Trade) <= 64, "Trade should fit in a single cache line");

// Engine-facing input event. The matcher consumes a stream of these.
enum class EventKind : std::uint8_t { Add, Cancel, Modify, MarketOrder };

struct OrderEvent {
    EventKind kind;
    Side      side;
    OrdType   type;
    std::uint8_t _pad;
    Quantity  qty;
    Price     price;
    OrderId   id;
    Timestamp ts;
};
static_assert(sizeof(OrderEvent) <= 64, "OrderEvent should fit in a single cache line");

} // namespace nanomatch
