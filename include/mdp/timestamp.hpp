#pragma once
#include <cstdint>
#include <ctime>

namespace mdp {

// ── Spin-wait hint ────────────────────────────────────────────────────────────
// Tells the CPU we are in a busy-wait loop:
//   - x86-64: PAUSE reduces pipeline pressure and power while spinning.
//   - ARM64:  YIELD is the equivalent hint.
//   - others: compiler barrier only (prevents the loop from being optimized out).
inline void cpu_pause() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

// ── Nanosecond monotonic clock ────────────────────────────────────────────────
// Used for all latency probes and throughput timing.
//   - Linux: CLOCK_MONOTONIC_RAW — not adjusted by NTP, so the tick rate is
//     stable. Ideal for subtracting two nearby readings.
//   - Other platforms: CLOCK_MONOTONIC (never goes backwards, may be slewed).
// Both threads read the same system-wide clock, which is what makes the
// cross-thread subtraction (consumer time - producer stamp) meaningful.
inline uint64_t now_ns() noexcept {
    struct timespec ts;
#ifdef __linux__
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + ts.tv_nsec;
}

} // namespace mdp
