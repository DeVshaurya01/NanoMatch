#include "nanomatch/itch_parser.hpp"

#include <cstring>

#if defined(_MSC_VER)
  #include <stdlib.h>
  #define BSWAP16(x) _byteswap_ushort(x)
  #define BSWAP32(x) _byteswap_ulong(x)
  #define BSWAP64(x) _byteswap_uint64(x)
#else
  #define BSWAP16(x) __builtin_bswap16(x)
  #define BSWAP32(x) __builtin_bswap32(x)
  #define BSWAP64(x) __builtin_bswap64(x)
#endif

namespace nanomatch {

namespace {

inline std::uint16_t be16(const std::byte* p) noexcept {
    std::uint16_t v; std::memcpy(&v, p, 2); return BSWAP16(v);
}
inline std::uint32_t be32(const std::byte* p) noexcept {
    std::uint32_t v; std::memcpy(&v, p, 4); return BSWAP32(v);
}
inline std::uint64_t be64(const std::byte* p) noexcept {
    std::uint64_t v; std::memcpy(&v, p, 8); return BSWAP64(v);
}
// 48-bit big-endian timestamp encoded as 6 bytes.
inline std::uint64_t be48(const std::byte* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v = (v << 8) | static_cast<std::uint8_t>(p[i]);
    return v;
}

// Offsets within the message *body* (after msg_type byte). All from the
// ITCH 5.0 spec; documented inline so the layout is greppable.
// 'A' Add Order            -- 35 bytes body
//   [0..2)   stock_locate (u16)
//   [2..4)   tracking_number (u16)
//   [4..10)  timestamp (u48 ns since midnight)
//   [10..18) order_ref_number (u64)
//   [18..19) side ('B' or 'S')
//   [19..23) shares (u32)
//   [23..31) stock (char[8])
//   [31..35) price (u32 fixed-point, 4 implied decimals)

} // namespace

ItchStats parse_itch_buffer(const std::byte* data, std::size_t size,
                            const ItchEventSink& sink) {
    ItchStats st{};
    const std::byte* p   = data;
    const std::byte* end = data + size;

    while (p + 2 <= end) {
        const std::uint16_t mlen = be16(p);
        p += 2;
        if (p + mlen > end) break;        // truncated tail
        const char type = static_cast<char>(p[0]);
        const std::byte* body = p + 1;
        ++st.messages;

        switch (type) {
        case 'A': {
            if (mlen < 36) { ++st.skipped; break; }
            OrderEvent ev{};
            ev.kind  = EventKind::Add;
            ev.ts    = be48(body + 4);
            ev.id    = be64(body + 10);
            ev.side  = (static_cast<char>(body[18]) == 'B') ? Side::Buy : Side::Sell;
            ev.qty   = be32(body + 19);
            ev.price = static_cast<Price>(be32(body + 31));
            ev.type  = OrdType::Limit;
            sink(ev);
            ++st.adds;
            break;
        }
        case 'F': {
            // Same as 'A' but with 4 trailing chars (MPID) we ignore.
            if (mlen < 40) { ++st.skipped; break; }
            OrderEvent ev{};
            ev.kind  = EventKind::Add;
            ev.ts    = be48(body + 4);
            ev.id    = be64(body + 10);
            ev.side  = (static_cast<char>(body[18]) == 'B') ? Side::Buy : Side::Sell;
            ev.qty   = be32(body + 19);
            ev.price = static_cast<Price>(be32(body + 31));
            ev.type  = OrdType::Limit;
            sink(ev);
            ++st.adds;
            break;
        }
        case 'E': {
            // Order Executed: id (u64 @10), shares (u32 @18), match# (@22)
            if (mlen < 31) { ++st.skipped; break; }
            OrderEvent ev{};
            ev.kind = EventKind::Cancel;        // matcher treats partial fills
            ev.id   = be64(body + 10);          // separately; we use Cancel
            ev.qty  = be32(body + 18);          // to mean "remove qty"
            ev.ts   = be48(body + 4);
            sink(ev);
            ++st.execs;
            break;
        }
        case 'X': {
            // Order Cancel (partial): id @10, shares @18
            if (mlen < 23) { ++st.skipped; break; }
            OrderEvent ev{};
            ev.kind = EventKind::Cancel;
            ev.id   = be64(body + 10);
            ev.qty  = be32(body + 18);
            ev.ts   = be48(body + 4);
            sink(ev);
            ++st.cancels;
            break;
        }
        case 'D': {
            // Order Delete (full): id @10
            if (mlen < 19) { ++st.skipped; break; }
            OrderEvent ev{};
            ev.kind = EventKind::Cancel;
            ev.id   = be64(body + 10);
            ev.qty  = 0;                         // 0 means "delete remainder"
            ev.ts   = be48(body + 4);
            sink(ev);
            ++st.deletes;
            break;
        }
        case 'U': {
            // Order Replace: old_id @10, new_id @18, new_shares @26, new_price @30
            if (mlen < 35) { ++st.skipped; break; }
            const Timestamp ts     = be48(body + 4);
            const OrderId   old_id = be64(body + 10);
            const OrderId   new_id = be64(body + 18);
            const Quantity  qty    = be32(body + 26);
            const Price     price  = static_cast<Price>(be32(body + 30));
            OrderEvent cancel{};
            cancel.kind = EventKind::Cancel;
            cancel.id = old_id; cancel.qty = 0; cancel.ts = ts;
            sink(cancel);
            // Replace adds with new id/price/qty -- side is unknown from U
            // alone (a real engine looks it up in its order table). We pass
            // it through as Add with side=Buy as a placeholder; matcher should
            // be tolerant or upstream should fix side.
            OrderEvent add{};
            add.kind = EventKind::Add;
            add.id = new_id; add.price = price; add.qty = qty; add.ts = ts;
            add.side = Side::Buy; add.type = OrdType::Limit;
            sink(add);
            ++st.replaces;
            break;
        }
        default:
            // 'S','R','H','C','P','Q','B','L','V','W','K','J','I','N' all
            // cold/admin; the matcher doesn't model them.
            ++st.skipped;
            break;
        }
        p += mlen;
    }
    return st;
}

} // namespace nanomatch
