#pragma once
#include <mdp/messages.hpp>

namespace mdp {

// Converts a RawMessage (exchange-native units) into a canonical MarketEvent.
//
// Price conversion: raw is integer cents (price * 100); canonical is
// fixed-point with 4 decimal places (price * 10000).
//     price_fp = price_raw * 100
//
// Stateless and allocation-free — safe on the hot path.

[[nodiscard]] inline MarketEvent normalize(const RawMessage& raw) noexcept {
    MarketEvent ev{};
    ev.type         = raw.type;
    ev.venue_id     = raw.venue_id;
    ev.order_id     = raw.order_id;
    ev.timestamp_ns = raw.timestamp_ns;
    ev.price_fp     = static_cast<int64_t>(raw.price_raw) * 100;
    ev.quantity     = raw.quantity;
    ev.side         = raw.side;
    ev.aggressor_id = raw.aggressor_id;
    return ev;
}

} // namespace mdp
