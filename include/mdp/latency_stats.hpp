#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mdp {

// ── Linear-bucket latency histogram ───────────────────────────────────────────
//
// Buckets 0 .. kBuckets-2 cover [0, (kBuckets-1) * 100ns) at 100 ns
// resolution. The last bucket is a catch-all for anything above that range,
// so tail percentiles still include large outliers.
//
// Not thread-safe by design: it is written only by the consumer thread and
// read from the main thread after join() — no sharing, no atomics needed.

struct LatencySummary {
    uint64_t count;
    uint64_t min_ns;
    uint64_t mean_ns;
    uint64_t p50_ns;
    uint64_t p90_ns;
    uint64_t p99_ns;
    uint64_t max_ns;
};

class LatencyHistogram {
public:
    static constexpr std::size_t kBuckets  = 1'000;
    static constexpr uint64_t    kBucketNs = 100;   // 100 ns per bucket

    void record(uint64_t latency_ns) noexcept {
        ++count_;
        total_ns_ += latency_ns;
        if (latency_ns < min_ns_) min_ns_ = latency_ns;
        if (latency_ns > max_ns_) max_ns_ = latency_ns;

        const std::size_t b = static_cast<std::size_t>(latency_ns / kBucketNs);
        ++buckets_[b < kBuckets ? b : kBuckets - 1];
    }

    // Nearest-rank percentile: returns the upper edge of the bucket holding
    // the p-th sample. The catch-all bucket reports max_ns (not sub-bucketed).
    [[nodiscard]] uint64_t percentile(double p) const noexcept {
        if (count_ == 0) return 0;
        const uint64_t target = std::max<uint64_t>(
            1, static_cast<uint64_t>(std::ceil(static_cast<double>(count_) * p)));
        uint64_t cum = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            cum += buckets_[i];
            if (cum >= target)
                return (i == kBuckets - 1) ? max_ns_ : (i + 1) * kBucketNs;
        }
        return max_ns_;
    }

    [[nodiscard]] LatencySummary summarize() const noexcept {
        return {
            .count   = count_,
            .min_ns  = count_ ? min_ns_ : 0,
            .mean_ns = count_ ? total_ns_ / count_ : 0,
            .p50_ns  = percentile(0.50),
            .p90_ns  = percentile(0.90),
            .p99_ns  = percentile(0.99),
            .max_ns  = max_ns_,
        };
    }

    [[nodiscard]] uint64_t count() const noexcept { return count_; }

private:
    std::array<uint64_t, kBuckets> buckets_{};
    uint64_t count_{0};
    uint64_t total_ns_{0};
    uint64_t min_ns_{UINT64_MAX};
    uint64_t max_ns_{0};
};

// Printing lives outside the collector — called after join(), off the hot path.
inline void print_latency_summary(const LatencySummary& s, const char* label = "Latency") {
    if (s.count == 0) {
        std::printf("\n── %s (no samples) ──\n", label);
        return;
    }
    std::printf("\n── %s (%llu samples) ──\n",
                label, static_cast<unsigned long long>(s.count));
    std::printf("  min  : %8llu ns\n", static_cast<unsigned long long>(s.min_ns));
    std::printf("  mean : %8llu ns\n", static_cast<unsigned long long>(s.mean_ns));
    std::printf("  p50  : %8llu ns\n", static_cast<unsigned long long>(s.p50_ns));
    std::printf("  p90  : %8llu ns\n", static_cast<unsigned long long>(s.p90_ns));
    std::printf("  p99  : %8llu ns\n", static_cast<unsigned long long>(s.p99_ns));
    std::printf("  max  : %8llu ns\n", static_cast<unsigned long long>(s.max_ns));
}

} // namespace mdp
