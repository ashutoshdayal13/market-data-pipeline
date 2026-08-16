#pragma once
#include <thread>
#include <utility>

namespace mdp {

// Optional per-thread CPU pinning (Linux only). Pinning producer and consumer
// to dedicated cores avoids scheduler migration and cache-refill noise, which
// matters when measuring nanosecond-scale latency.
//
// Configure with env vars: MDP_PRODUCER_CPU / MDP_CONSUMER_CPU.
// Unset means no pinning (and non-Linux platforms ignore it).

struct CpuAffinityConfig {
    int producer_cpu{-1};
    int consumer_cpu{-1};
};

[[nodiscard]] CpuAffinityConfig cpu_affinity_from_env() noexcept;

// Pin the calling thread to `cpu` (>= 0). Returns true on success (Linux).
[[nodiscard]] bool pin_current_thread(int cpu) noexcept;

// Launch a thread that pins itself to `cpu` (if >= 0) before running fn.
template <typename F>
[[nodiscard]] std::thread spawn_on_cpu(int cpu, F&& fn) {
    return std::thread([cpu, fn = std::forward<F>(fn)]() mutable {
        (void)pin_current_thread(cpu);
        fn();
    });
}

} // namespace mdp
