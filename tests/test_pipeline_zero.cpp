#include <mdp/pipeline_runner.hpp>
#include <mdp/strategy.hpp>
#include <cmath>
#include <cstdio>

// Smoke test: a zero-message run must terminate cleanly and report zeros —
// no divide-by-zero, no spurious latency samples, no hang.
int main() {
    mdp::NoOpStrategy noop;
    mdp::PipelineRunner runner{0, noop};

    runner.start();
    runner.wait();

    const auto r = runner.result();
    const auto s = runner.latency().summarize();

    int failures = 0;
    auto check = [&](bool ok, const char* msg) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", msg);
            ++failures;
        }
    };

    check(r.produced == 0,                "produced should be 0");
    check(r.consumed == 0,                "consumed should be 0");
    check(r.throughput_mps == 0.0,        "throughput should be 0");
    check(s.count == 0,                   "latency count should be 0");
    check(!std::isnan(r.throughput_mps) && !std::isinf(r.throughput_mps),
          "throughput must be finite");

    if (failures == 0)
        std::puts("PASS: zero-message pipeline run");

    return failures == 0 ? 0 : 1;
}
