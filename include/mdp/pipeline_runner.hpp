#pragma once
#include <mdp/messages.hpp>
#include <mdp/spsc_queue.hpp>
#include <mdp/pipeline_constants.hpp>
#include <mdp/feed_generator.hpp>
#include <mdp/feed_handler.hpp>
#include <mdp/strategy.hpp>
#include <mdp/latency_stats.hpp>
#include <mdp/timestamp.hpp>
#include <mdp/cpu_affinity.hpp>
#include <thread>
#include <cstddef>
#include <cstdint>

namespace mdp {

using Queue = SPSCQueue<RawMessage, kQueueCapacity>;

struct PipelineResult {
    double      throughput_mps;  // million messages per second
    uint64_t    elapsed_ns;
    std::size_t produced;
    std::size_t consumed;
};

// ── PipelineRunner ────────────────────────────────────────────────────────────
//
// Owns the queue, the producer thread (FeedGenerator), and the consumer
// thread (FeedHandler). main() stays a thin wiring layer.
//
// Rule: no I/O of any kind inside start() or on either thread. All printing
// happens in the caller after wait() returns — the hot path is never stalled
// by a write syscall or buffer flush.

class PipelineRunner {
public:
    PipelineRunner(std::size_t n_messages, Strategy& strategy)
        : n_messages_(n_messages)
        , generator_(queue_, n_messages)
        , handler_(queue_, strategy, &latency_) {}

    // Launch producer and consumer threads (non-blocking).
    void start() {
        const CpuAffinityConfig affinity = cpu_affinity_from_env();
        start_ns_ = now_ns();
        producer_thread_ = spawn_on_cpu(affinity.producer_cpu, [this] { generator_.run(); });
        consumer_thread_ = spawn_on_cpu(affinity.consumer_cpu,
                                        [this] { handler_.run(n_messages_); });
    }

    // Block until both threads complete.
    void wait() {
        producer_thread_.join();
        consumer_thread_.join();
        end_ns_ = now_ns();
    }

    [[nodiscard]] PipelineResult result() const noexcept {
        const uint64_t elapsed = end_ns_ > start_ns_ ? end_ns_ - start_ns_ : 1;
        const double   seconds = static_cast<double>(elapsed) / 1e9;
        return {
            .throughput_mps = n_messages_ == 0
                ? 0.0
                : static_cast<double>(n_messages_) / seconds / 1e6,
            .elapsed_ns = elapsed,
            .produced   = generator_.produced(),
            .consumed   = handler_.consumed(),
        };
    }

    [[nodiscard]] const LatencyHistogram& latency() const noexcept { return latency_; }

private:
    std::size_t                   n_messages_;
    Queue                         queue_;
    FeedGenerator<kQueueCapacity> generator_;
    FeedHandler<kQueueCapacity>   handler_;
    LatencyHistogram              latency_;
    std::thread                   producer_thread_;
    std::thread                   consumer_thread_;
    uint64_t                      start_ns_{0};
    uint64_t                      end_ns_{0};
};

} // namespace mdp
