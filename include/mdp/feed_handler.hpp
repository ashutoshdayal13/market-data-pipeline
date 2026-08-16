#pragma once
#include <mdp/messages.hpp>
#include <mdp/spsc_queue.hpp>
#include <mdp/strategy.hpp>
#include <cstddef>

namespace mdp {

class LatencyHistogram;

// ── FeedHandler ───────────────────────────────────────────────────────────────
//
// Consumer side: runs on its own thread, drains the SPSC queue, normalizes
// each RawMessage into a MarketEvent, and dispatches it to the strategy.
//
// When a histogram is provided, records pipeline latency per message:
//     producer push stamp  →  post-normalize, pre-callback.
// Strategy work is deliberately excluded from the measurement.

template <std::size_t Capacity>
class FeedHandler {
public:
    FeedHandler(SPSCQueue<RawMessage, Capacity>& q,
                Strategy& strategy,
                LatencyHistogram* latency = nullptr)
        : queue_(q), strategy_(strategy), latency_(latency) {}

    // Consume exactly n_messages, then return the count consumed.
    std::size_t run(std::size_t n_messages) noexcept;

    [[nodiscard]] std::size_t consumed() const noexcept { return consumed_; }

private:
    SPSCQueue<RawMessage, Capacity>& queue_;
    Strategy&                        strategy_;
    LatencyHistogram*                latency_;
    std::size_t                      consumed_{0};
};

} // namespace mdp
