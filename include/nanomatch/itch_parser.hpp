// Brick 10 (part 2) — NASDAQ TotalView-ITCH 5.0 parser.
// Streams a sequence of length-prefixed messages (the format used by the
// `S<date>-v50.txt` raw binary files from emi.nasdaqomxtrader.com -- *NOT*
// the PCAP wrapper, which has MoldUDP framing on top).
//
// Wire layout per message (big-endian):
//   uint16_t  msg_len           // does NOT include the 2 length bytes
//   uint8_t   msg_type          // 'A','F','E','C','X','D','U','S','R',...
//   ...                         // type-dependent body
//
// We dispatch on msg_type, byte-swap, populate an OrderEvent, hand to a
// callable. Cold message types are routed to a separate cold path so the hot
// dispatch stays small and L1i-resident.
#pragma once

#include "types.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace nanomatch {

struct ItchStats {
    std::uint64_t messages = 0;
    std::uint64_t adds     = 0;
    std::uint64_t execs    = 0;
    std::uint64_t cancels  = 0;
    std::uint64_t deletes  = 0;
    std::uint64_t replaces = 0;
    std::uint64_t skipped  = 0;
};

// Callback signature: const OrderEvent& -> void. Use a function pointer or a
// small lambda (don't capture much state if you want the inline to stick).
using ItchEventSink = std::function<void(const OrderEvent&)>;

// Parse a contiguous buffer. Returns stats. Stops on truncated trailing msg.
ItchStats parse_itch_buffer(const std::byte* data, std::size_t size,
                            const ItchEventSink& sink);

} // namespace nanomatch
