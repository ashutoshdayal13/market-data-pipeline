#include <mdp/cpu_affinity.hpp>
#include <cstdlib>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace mdp {

CpuAffinityConfig cpu_affinity_from_env() noexcept {
    CpuAffinityConfig cfg;
    if (const char* s = std::getenv("MDP_PRODUCER_CPU")) {
        if (*s) cfg.producer_cpu = std::atoi(s);
    }
    if (const char* s = std::getenv("MDP_CONSUMER_CPU")) {
        if (*s) cfg.consumer_cpu = std::atoi(s);
    }
    return cfg;
}

bool pin_current_thread(int cpu) noexcept {
    if (cpu < 0) return false;
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<unsigned>(cpu), &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)cpu;
    return false;
#endif
}

} // namespace mdp
