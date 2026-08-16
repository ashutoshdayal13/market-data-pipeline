#pragma once
#include <atomic>
#include <array>
#include <cstddef>

namespace mdp {

// ── SPSC Lock-Free Ring Buffer ────────────────────────────────────────────────
//
// Single-Producer / Single-Consumer queue. Because exactly one thread pushes
// and exactly one thread pops, each index has a single writer:
//
//     tail — written only by the producer (advanced after writing a slot)
//     head — written only by the consumer (advanced after reading a slot)
//
// That is why no compare-and-swap (CAS) is needed anywhere: neither thread
// ever competes for a slot. Plain stores with the right memory ordering are
// enough.
//
// Memory ordering (the core idea):
//     push:  write buf_[tail]           →  tail.store(release)
//     pop:   tail.load(acquire)         →  read buf_[head]  →  head.store(release)
//
// The release-store on tail guarantees the slot write is visible to the
// consumer BEFORE the consumer observes the new tail (acquire-load pairs with
// it). Symmetrically for head on the producer's full-check. seq_cst is not
// needed — we only require this pairwise happens-before, not a global order.
//
// Cache-line separation: tail and head live on separate 64-byte-aligned
// structs. If they shared a cache line, every producer store to tail would
// invalidate the consumer's cached line (false sharing). Each side also keeps
// a local snapshot of the other index (cached_head / cached_tail) so it only
// touches the remote cache line when the snapshot says full/empty.
//
// Capacity must be a power of 2 so index wrapping is a bitmask, not a modulo.
// One slot is intentionally unused: head == tail means empty, and
// (tail + 1) == head means full. Max stored elements = Capacity - 1.

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 and >= 2");

    static constexpr std::size_t kMask      = Capacity - 1;
    static constexpr std::size_t kCacheLine = 64;  // x86-64 / ARM64 line size

    // Producer-owned cache line.
    struct alignas(kCacheLine) ProducerSide {
        std::atomic<std::size_t> tail{0};
        std::size_t              cached_head{0};
    } prod_;

    // Consumer-owned cache line (separate from the producer's).
    struct alignas(kCacheLine) ConsumerSide {
        std::atomic<std::size_t> head{0};
        std::size_t              cached_tail{0};
    } cons_;

    // Data slots, aligned separately from either control line.
    alignas(kCacheLine) std::array<T, Capacity> buf_;

public:
    SPSCQueue() = default;

    // Producer side. Returns false when the queue is full (non-blocking).
    [[nodiscard]] bool push(const T& item) noexcept {
        const std::size_t tail      = prod_.tail.load(std::memory_order_relaxed);
        const std::size_t next_tail = (tail + 1) & kMask;

        // Full-check against the local snapshot first; refresh it from the
        // consumer's line only when the snapshot says full.
        if (next_tail == prod_.cached_head) {
            prod_.cached_head = cons_.head.load(std::memory_order_acquire);
            if (next_tail == prod_.cached_head)
                return false;  // genuinely full
        }

        buf_[tail] = item;                                        // 1. write slot
        prod_.tail.store(next_tail, std::memory_order_release);   // 2. publish
        return true;
    }

    // Consumer side. Returns false when the queue is empty (non-blocking).
    [[nodiscard]] bool pop(T& item) noexcept {
        const std::size_t head = cons_.head.load(std::memory_order_relaxed);

        if (head == cons_.cached_tail) {
            cons_.cached_tail = prod_.tail.load(std::memory_order_acquire);
            if (head == cons_.cached_tail)
                return false;  // genuinely empty
        }

        item = buf_[head];                                            // 1. read slot
        cons_.head.store((head + 1) & kMask, std::memory_order_release);  // 2. free it
        return true;
    }

    // ── Introspection (approximate — for diagnostics/pacing only) ────────────
    [[nodiscard]] bool empty() const noexcept {
        return cons_.head.load(std::memory_order_acquire) ==
               prod_.tail.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t head = cons_.head.load(std::memory_order_acquire);
        const std::size_t tail = prod_.tail.load(std::memory_order_acquire);
        return (tail - head + Capacity) & kMask;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }
};

} // namespace mdp
