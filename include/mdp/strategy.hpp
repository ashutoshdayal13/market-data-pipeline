#pragma once
#include <mdp/messages.hpp>
#include <cstdint>

namespace mdp {

// ── Strategy interface ────────────────────────────────────────────────────────
//
// Polymorphic callback for consuming market events. Virtual dispatch costs a
// few ns per call but lets strategies be swapped at runtime without touching
// the pipeline (e.g. NoOpStrategy for clean throughput baselines vs
// StatsStrategy for event counting).
//
// onMarketEvent is noexcept: the consumer thread has no exception handler, so
// a throwing strategy would terminate the process. Implementations must not
// allocate or do I/O on the hot path.

class Strategy {
public:
    virtual void onMarketEvent(const MarketEvent& event) noexcept = 0;
    virtual ~Strategy() = default;
};

// Baseline: zero work per event, used to measure pure pipeline overhead.
class NoOpStrategy final : public Strategy {
public:
    void onMarketEvent(const MarketEvent&) noexcept override {}
};

// Counts events per message type. Touched only by the consumer thread,
// read from main after join() — no atomics needed.
class StatsStrategy final : public Strategy {
public:
    void onMarketEvent(const MarketEvent& event) noexcept override {
        ++total_events_;
        switch (event.type) {
            case MsgType::Add:    ++add_count_;    break;
            case MsgType::Cancel: ++cancel_count_; break;
            case MsgType::Modify: ++modify_count_; break;
            case MsgType::Trade:  ++trade_count_;  break;
        }
    }

    void print_stats() const;

    [[nodiscard]] uint64_t total_events() const noexcept { return total_events_; }
    [[nodiscard]] uint64_t add_count()    const noexcept { return add_count_; }
    [[nodiscard]] uint64_t cancel_count() const noexcept { return cancel_count_; }
    [[nodiscard]] uint64_t modify_count() const noexcept { return modify_count_; }
    [[nodiscard]] uint64_t trade_count()  const noexcept { return trade_count_; }

private:
    uint64_t total_events_{0};
    uint64_t add_count_{0};
    uint64_t cancel_count_{0};
    uint64_t modify_count_{0};
    uint64_t trade_count_{0};
};

} // namespace mdp
