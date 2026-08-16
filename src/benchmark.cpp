#include <mdp/pipeline_runner.hpp>
#include <mdp/strategy.hpp>
#include <mdp/latency_stats.hpp>
#include <mdp/timestamp.hpp>
#include <mdp/cpu_affinity.hpp>
#include <thread>
#include <atomic>
#include <array>
#include <algorithm>
#include <cstdio>

// ── Benchmark suite ───────────────────────────────────────────────────────────
//
// Reports three separate properties — they answer different questions and
// should not be compared to each other:
//
//   A. CAPACITY          how fast can the system move messages?
//      [1] full pipeline throughput (NoOp strategy, full blast)
//      [2] queue-only push/pop, single thread
//      [3] queue-only push/pop, two threads
//
//   B. INTRINSIC LATENCY push → pre-callback with max_in_flight=1
//      [4] producer waits for an empty queue before each push, so the
//          probe measures transit + normalize, not queue wait
//
//   C. OVERLOAD          same probe at full blast (max_in_flight=0)
//      [5] producer outruns consumer → ring fills → p50/p99 ≈ time to
//          drain a full ring (expected and correct, not a bug)
//
// Run on pinned cores for stable numbers:
//   MDP_PRODUCER_CPU=2 MDP_CONSUMER_CPU=3 taskset -c 2,3 ./benchmark

namespace {

constexpr std::size_t kWarmup     = 200'000;
constexpr std::size_t kMeasure    = 5'000'000;
constexpr std::size_t kPacedMsgs  = 1'000'000;
constexpr int         kTrials     = 3;
constexpr int         kBatch      = 64;

// Run the full pipeline once; returns throughput in M msg/s.
double run_pipeline(std::size_t n,
                    mdp::Strategy& strategy,
                    mdp::LatencyHistogram* latency = nullptr,
                    std::size_t max_in_flight = 0) {
    mdp::Queue queue;
    mdp::FeedGenerator<mdp::kQueueCapacity> gen{queue, n, max_in_flight};
    mdp::FeedHandler<mdp::kQueueCapacity>   handler{queue, strategy, latency};

    const mdp::CpuAffinityConfig affinity = mdp::cpu_affinity_from_env();
    const uint64_t t0 = mdp::now_ns();
    auto producer = mdp::spawn_on_cpu(affinity.producer_cpu, [&] { gen.run(); });
    auto consumer = mdp::spawn_on_cpu(affinity.consumer_cpu, [&] { handler.run(n); });
    producer.join();
    consumer.join();
    const uint64_t elapsed = mdp::now_ns() - t0;

    return static_cast<double>(n) / (static_cast<double>(elapsed) / 1e9) / 1e6;
}

// Queue-only microbenchmark: one thread alternates batched push/pop.
double bench_queue_single_thread(std::size_t n) {
    mdp::Queue queue;
    mdp::RawMessage msg{};
    msg.type = mdp::MsgType::Add;

    const uint64_t t0 = mdp::now_ns();
    std::size_t pushed = 0, popped = 0;
    while (popped < n) {
        for (int i = 0; i < kBatch && pushed < n; ++i)
            if (queue.push(msg)) ++pushed;
        for (int i = 0; i < kBatch && popped < pushed; ++i) {
            mdp::RawMessage tmp;
            if (queue.pop(tmp)) ++popped;
        }
    }
    const uint64_t elapsed = mdp::now_ns() - t0;
    return static_cast<double>(n) / (static_cast<double>(elapsed) / 1e9) / 1e6;
}

// Queue-only microbenchmark: producer and consumer on separate threads.
double bench_queue_dual_thread(std::size_t n) {
    mdp::Queue queue;
    mdp::RawMessage msg{};
    msg.type = mdp::MsgType::Add;

    const mdp::CpuAffinityConfig affinity = mdp::cpu_affinity_from_env();
    std::atomic<bool> start{false};

    auto producer = mdp::spawn_on_cpu(affinity.producer_cpu, [&] {
        while (!start.load(std::memory_order_acquire)) mdp::cpu_pause();
        for (std::size_t i = 0; i < n; ++i)
            while (!queue.push(msg)) mdp::cpu_pause();
    });
    auto consumer = mdp::spawn_on_cpu(affinity.consumer_cpu, [&] {
        while (!start.load(std::memory_order_acquire)) mdp::cpu_pause();
        mdp::RawMessage tmp{};
        for (std::size_t i = 0; i < n; ++i)
            while (!queue.pop(tmp)) mdp::cpu_pause();
    });

    const uint64_t t0 = mdp::now_ns();
    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    const uint64_t elapsed = mdp::now_ns() - t0;
    return static_cast<double>(n) / (static_cast<double>(elapsed) / 1e9) / 1e6;
}

double median3(std::array<double, kTrials> v) {
    std::sort(v.begin(), v.end());
    return v[kTrials / 2];
}

void print_throughput(const char* label, std::array<double, kTrials> runs) {
    std::printf("  %-38s : %6.2f M msg/s (median of %d: %.2f / %.2f / %.2f)\n",
                label, median3(runs), kTrials, runs[0], runs[1], runs[2]);
}

void print_latency(const char* label, const mdp::LatencySummary& s) {
    std::printf("\n  %s\n", label);
    std::printf("    min : %8llu ns    p50 : %8llu ns\n",
                static_cast<unsigned long long>(s.min_ns),
                static_cast<unsigned long long>(s.p50_ns));
    std::printf("    p90 : %8llu ns    p99 : %8llu ns\n",
                static_cast<unsigned long long>(s.p90_ns),
                static_cast<unsigned long long>(s.p99_ns));
    std::printf("    max : %8llu ns\n",
                static_cast<unsigned long long>(s.max_ns));
}

} // namespace

int main() {
    std::puts("──────────────────────────────────────────────────");
    std::puts("  market-data-pipeline · benchmark suite");
    std::puts("  Pin cores for stable numbers (Linux):");
    std::puts("    MDP_PRODUCER_CPU=2 MDP_CONSUMER_CPU=3 taskset -c 2,3 ./benchmark");
    std::puts("──────────────────────────────────────────────────\n");

    mdp::NoOpStrategy noop;

    // Warm-up: fault in the queue memory, spin up caches and branch predictors.
    run_pipeline(kWarmup, noop);

    // ── A. Capacity ──────────────────────────────────────────────────────────
    std::puts("── A. CAPACITY ──────────────────────────────────");
    {
        std::array<double, kTrials> runs{};
        for (int i = 0; i < kTrials; ++i)
            runs[i] = run_pipeline(kMeasure, noop);
        print_throughput("[1] Pipeline throughput (NoOp)", runs);
    }
    {
        std::array<double, kTrials> runs{};
        for (int i = 0; i < kTrials; ++i)
            runs[i] = bench_queue_single_thread(kMeasure);
        print_throughput("[2] Queue push/pop (1 thread)", runs);
    }
    {
        std::array<double, kTrials> runs{};
        for (int i = 0; i < kTrials; ++i)
            runs[i] = bench_queue_dual_thread(kMeasure);
        print_throughput("[3] Queue push/pop (2 threads)", runs);
    }

    // ── B. Intrinsic latency (low backlog) ───────────────────────────────────
    std::puts("\n── B. INTRINSIC LATENCY (max_in_flight=1) ───────");
    {
        mdp::LatencyHistogram latency;
        run_pipeline(kPacedMsgs, noop, &latency, /*max_in_flight=*/1);
        print_latency("[4] Paced pipeline latency — transit + normalize, no queue wait",
                      latency.summarize());
    }

    // ── C. Queueing under overload ───────────────────────────────────────────
    std::puts("\n── C. OVERLOAD (max_in_flight=0) ────────────────");
    {
        mdp::LatencyHistogram latency;
        run_pipeline(kMeasure, noop, &latency, /*max_in_flight=*/0);
        print_latency("[5] Full-blast latency — ring fills, queue wait dominates",
                      latency.summarize());
        std::puts("    (p50 ≈ p99 ≈ time to drain a full ring — expected under overload)");
    }

    std::puts("");
    return 0;
}
