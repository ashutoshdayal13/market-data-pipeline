#include <mdp/feed_handler.hpp>
#include <mdp/normalizer.hpp>
#include <mdp/latency_stats.hpp>
#include <mdp/pipeline_constants.hpp>
#include <mdp/timestamp.hpp>

namespace mdp {

template <std::size_t Capacity>
std::size_t FeedHandler<Capacity>::run(std::size_t n_messages) noexcept {
    RawMessage raw{};

    while (consumed_ < n_messages) {
        if (!queue_.pop(raw)) {
            // Empty: spin with a pause hint until the producer publishes.
            cpu_pause();
            continue;
        }

        const MarketEvent event = normalize(raw);

        if (latency_) {
            const uint64_t now = now_ns();
            // Guard: if the origin stamp is somehow ahead of now (clock step),
            // the unsigned subtraction would wrap to a huge value and poison
            // the histogram — drop such samples instead.
            if (now >= event.timestamp_ns)
                latency_->record(now - event.timestamp_ns);
        }

        strategy_.onMarketEvent(event);
        ++consumed_;
    }

    return consumed_;
}

template class FeedHandler<kQueueCapacity>;

} // namespace mdp
