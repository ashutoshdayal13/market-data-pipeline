#pragma once
#include <cstdint>
#include <type_traits>

namespace mdp {

// ── Message types ─────────────────────────────────────────────────────────────

enum class MsgType : uint8_t { Add, Cancel, Modify, Trade };

// Raw message as it arrives from the (synthetic) feed.
// Price is in integer cents: 10050 == $100.50.
// timestamp_ns is stamped by the producer immediately before push — it is the
// origin point for the pipeline latency measurement.
// Fields not used by a given message type are left zero (e.g. Cancel carries
// only order_id).
struct RawMessage {
    MsgType  type;
    uint32_t venue_id;
    uint64_t order_id;
    uint64_t timestamp_ns;
    int32_t  price_raw;     // integer cents
    uint32_t quantity;
    char     side;          // 'B', 'S', or '\0'
    uint64_t aggressor_id;  // Trade only
};

// Plain data with no pointers or heap — safe to copy through the queue.
static_assert(std::is_trivially_copyable_v<RawMessage>);

// Canonical event delivered to strategy callbacks.
// Price is fixed-point with 4 decimal places: price_fp = price * 10000.
// Integers (not floating point) keep results deterministic and
// bitwise-comparable across platforms.
struct MarketEvent {
    MsgType  type;
    uint32_t venue_id;
    uint64_t order_id;
    uint64_t timestamp_ns;  // preserved from RawMessage (latency probe origin)
    int64_t  price_fp;      // fixed-point, 4 decimal places
    uint32_t quantity;
    char     side;
    uint64_t aggressor_id;
};

} // namespace mdp
