#include <mdp/feed_generator.hpp>
#include <mdp/pipeline_constants.hpp>
#include <mdp/timestamp.hpp>
#include <cstdint>

namespace mdp {

namespace {
// Cheap deterministic pseudo-random generator (64-bit LCG). Avoids the cost
// and global state of stdlib rand() on the producer thread.
inline uint64_t lcg(uint64_t& state) noexcept {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
}
} // namespace

template <std::size_t Capacity>
RawMessage FeedGenerator<Capacity>::make_message(uint64_t seq) const noexcept {
    uint64_t rng = seq + 1;  // seed from seq → deterministic but varied

    RawMessage m{};
    m.venue_id = static_cast<uint32_t>((lcg(rng) % 4) + 1);  // venues 1–4
    m.order_id = seq + 1;

    // Mix: 50% Add, 20% Cancel, 20% Modify, 10% Trade
    const uint64_t roll = lcg(rng) % 100;
    if      (roll < 50) m.type = MsgType::Add;
    else if (roll < 70) m.type = MsgType::Cancel;
    else if (roll < 90) m.type = MsgType::Modify;
    else                m.type = MsgType::Trade;

    // Price: $90.00–$110.00 as integer cents (9000–11000)
    const int32_t price_raw = 10'000 + static_cast<int32_t>(lcg(rng) % 2001) - 1000;

    switch (m.type) {
        case MsgType::Add:
            m.price_raw = price_raw;
            m.quantity  = static_cast<uint32_t>((lcg(rng) % 100) + 1) * 100;
            m.side      = (lcg(rng) % 2) ? 'B' : 'S';
            break;
        case MsgType::Cancel:
            break;  // only order_id is meaningful
        case MsgType::Modify:
            m.price_raw = price_raw;
            m.quantity  = static_cast<uint32_t>((lcg(rng) % 100) + 1) * 100;
            break;
        case MsgType::Trade:
            m.price_raw    = price_raw;
            m.quantity     = static_cast<uint32_t>((lcg(rng) % 50) + 1) * 100;
            m.aggressor_id = seq + 1000;
            break;
    }

    return m;
}

template <std::size_t Capacity>
void FeedGenerator<Capacity>::run() noexcept {
    for (std::size_t i = 0; i < n_messages_; ++i) {
        // Low-backlog pacing: wait until occupancy drops below the limit so
        // the next push measures transit, not time queued behind a backlog.
        if (max_in_flight_ > 0) {
            while (queue_.size_approx() >= max_in_flight_)
                cpu_pause();
        }

        RawMessage msg = make_message(static_cast<uint64_t>(i));

        // Stamp as late as possible — immediately before push — so the
        // latency probe starts right at the publish point.
        msg.timestamp_ns = now_ns();

        // Backpressure policy: spin until space. No drops, no overwrites,
        // FIFO order preserved.
        while (!queue_.push(msg))
            cpu_pause();

        ++produced_;
    }
}

template class FeedGenerator<kQueueCapacity>;

} // namespace mdp
