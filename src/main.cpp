#include <mdp/pipeline_runner.hpp>
#include <mdp/strategy.hpp>
#include <mdp/latency_stats.hpp>
#include <cstdio>
#include <cstdlib>

// ── market-data-pipeline ──────────────────────────────────────────────────────
//
// main() is a pure wiring layer; all logic lives in reusable components:
//   FeedGenerator  — synthetic message producer (own thread)
//   FeedHandler    — consumer: pop → normalize → dispatch (own thread)
//   PipelineRunner — owns queue + threads, records latency
//   StatsStrategy  — per-message-type counters
//
// I/O policy: nothing is printed while the pipeline runs. All output happens
// after both threads have joined.
//
// Usage:
//   ./pipeline [n_messages]      (default 2,000,000)
//
// Stable numbers on Linux (pin each thread to its own core):
//   MDP_PRODUCER_CPU=2 MDP_CONSUMER_CPU=3 taskset -c 2,3 ./pipeline

static constexpr std::size_t kDefault = 2'000'000;

int main(int argc, char** argv) {
    const std::size_t n = (argc > 1)
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : kDefault;

    std::printf("market-data-pipeline | %zu messages | queue capacity %zu\n",
                n, mdp::kQueueCapacity);

    mdp::StatsStrategy  stats;
    mdp::PipelineRunner runner{n, stats};

    runner.start();
    runner.wait();

    // All I/O below this line — threads have joined.
    mdp::print_latency_summary(runner.latency().summarize(),
                               "Pipeline latency (push → pre-callback)");
    std::printf("\n");
    stats.print_stats();

    const auto r = runner.result();
    std::printf("── Throughput ────────────────────\n");
    std::printf("  wall time : %.3f s\n",   static_cast<double>(r.elapsed_ns) / 1e9);
    std::printf("  msgs/sec  : %.2f M/s\n", r.throughput_mps);
    std::printf("  produced  : %zu\n",      r.produced);
    std::printf("  consumed  : %zu\n",      r.consumed);

    return 0;
}
