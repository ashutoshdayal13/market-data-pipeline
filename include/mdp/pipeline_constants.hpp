#pragma once
#include <cstddef>

namespace mdp {

// Ring slot count for the pipeline queue.
// One slot is reserved to distinguish full from empty,
// so max in-flight messages = kQueueCapacity - 1.
inline constexpr std::size_t kQueueCapacity = 65536;

} // namespace mdp
