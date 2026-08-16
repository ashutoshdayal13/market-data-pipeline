#pragma once
#include <mdp/messages.hpp>
#include <mdp/spsc_queue.hpp>
#include <cstddef>
#include <cstdint>

namespace mdp {

// ── FeedGenerator ─────────────────────────────────────────────────────────────
//
// Producer side: generates synthetic RawMessages and pushes them into the
// SPSC queue. Message mix approximates real feeds:
//     50% Add, 20% Cancel, 20% Modify, 10% Trade.
//
// timestamp_ns is stamped with now_ns() immediately before push — as late as
// possible — so the latency measurement covers queue transit, not message
// construction.
//
// max_in_flight (optional):
//     0  → full blast: push as fast as possible, spin only when the ring
//          is completely full. Used for throughput / overload runs.
//     1  → low backlog: wait until the queue is empty before each push, so
//          the latency probe measures pure transit instead of time spent
//          queued behind a backlog. Used for intrinsic-latency runs.

template <std::size_t Capacity>
class FeedGenerator {
public:
    explicit FeedGenerator(SPSCQueue<RawMessage, Capacity>& q,
                           std::size_t n_messages,
                           std::size_t max_in_flight = 0)
        : queue_(q)
        , n_messages_(n_messages)
        , max_in_flight_(max_in_flight) {}

    void run() noexcept;

    [[nodiscard]] std::size_t produced() const noexcept { return produced_; }

private:
    RawMessage make_message(uint64_t seq) const noexcept;

    SPSCQueue<RawMessage, Capacity>& queue_;
    std::size_t                      n_messages_;
    std::size_t                      max_in_flight_{0};
    std::size_t                      produced_{0};
};

} // namespace mdp
