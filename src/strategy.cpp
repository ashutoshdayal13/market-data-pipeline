#include <mdp/strategy.hpp>
#include <cstdio>

namespace mdp {

void StatsStrategy::print_stats() const {
    std::printf("── Event counts ──────────────────\n");
    std::printf("  Total   : %llu\n", static_cast<unsigned long long>(total_events_));
    std::printf("  Add     : %llu\n", static_cast<unsigned long long>(add_count_));
    std::printf("  Cancel  : %llu\n", static_cast<unsigned long long>(cancel_count_));
    std::printf("  Modify  : %llu\n", static_cast<unsigned long long>(modify_count_));
    std::printf("  Trade   : %llu\n", static_cast<unsigned long long>(trade_count_));
    std::printf("\n");
}

} // namespace mdp
