#include <mdp/spsc_queue.hpp>
#include <mdp/messages.hpp>
#include <mdp/timestamp.hpp>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdint>

// ── Minimal test framework (no external dependencies) ─────────────────────────
static int g_pass = 0, g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) { ++g_pass; }                                            \
        else {                                                             \
            ++g_fail;                                                      \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n",                      \
                         __FILE__, __LINE__, #cond);                       \
        }                                                                  \
    } while (0)

using Q16 = mdp::SPSCQueue<int, 16>;

// Sum of the sequence [0, n). If the concurrent test's received data has the
// right size, right sum, AND is in order, nothing was lost or duplicated.
static int64_t expected_sum(int n) {
    return static_cast<int64_t>(n) * (n - 1) / 2;
}

// ── 1. Empty / full semantics ─────────────────────────────────────────────────
// Capacity 16 stores at most 15 elements (one slot reserved so that
// head == tail unambiguously means empty).
static void test_empty_and_full() {
    Q16 q;
    CHECK(q.empty());
    CHECK(q.size_approx() == 0);

    for (int i = 0; i < 15; ++i)
        CHECK(q.push(i));

    CHECK(!q.empty());
    CHECK(q.size_approx() == 15);
    CHECK(!q.push(99));  // full — must reject

    // Drain fully, then verify the queue is usable again. Ring-buffer bugs
    // (stale cached indices, off-by-one) often appear exactly at this
    // full → empty boundary.
    int v = -1;
    while (q.pop(v)) {}
    CHECK(q.empty());

    CHECK(q.push(42));
    CHECK(q.pop(v));
    CHECK(v == 42);
    CHECK(q.empty());
}

// ── 2. FIFO ordering ──────────────────────────────────────────────────────────
static void test_fifo_order() {
    Q16 q;
    for (int i = 0; i < 10; ++i)
        CHECK(q.push(i));
    for (int i = 0; i < 10; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK(v == i);
    }
    CHECK(q.empty());
}

// ── 3. Pop on empty ───────────────────────────────────────────────────────────
static void test_pop_empty() {
    Q16 q;
    int v = -1;
    CHECK(!q.pop(v));
    CHECK(v == -1);  // must not be modified on failure
}

// ── 4. Wraparound with head/tail crossing the ring boundary ───────────────────
// fill 15 → pop 10 → push 10 → drain. Head and tail wrap past index 0
// independently while elements are live — where index-arithmetic bugs hide.
static void test_wraparound_crossing() {
    Q16 q;

    for (int i = 0; i < 15; ++i) CHECK(q.push(i));
    for (int i = 0; i < 10; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK(v == i);
    }
    for (int i = 0; i < 10; ++i) CHECK(q.push(100 + i));
    CHECK(q.size_approx() == 15);

    const int expected[] = {10, 11, 12, 13, 14,
                            100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    for (int i = 0; i < 15; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK(v == expected[i]);
    }
    CHECK(q.empty());
}

// ── 5. Repeated fill/drain cycles ─────────────────────────────────────────────
// Push to full, pop to empty, many times. Catches slow corruption of the
// empty/full boundary that a single pass would miss.
static void test_fill_drain_repeat() {
    Q16 q;
    for (int round = 0; round < 10'000; ++round) {
        int pushed = 0;
        while (q.push(round)) ++pushed;
        CHECK(pushed == 15);

        int popped = 0, v = -1;
        while (q.pop(v)) {
            CHECK(v == round);
            ++popped;
        }
        CHECK(popped == 15);
        CHECK(q.empty());
    }
}

// ── 6. Concurrent correctness: 1M messages, real producer + consumer ─────────
// The core test: with two live threads, every message must arrive exactly
// once and in order. Size + checksum + order together prove no loss, no
// duplication, no reordering.
static void test_concurrent_correctness() {
    constexpr int kN = 1'000'000;
    mdp::SPSCQueue<int, 4096> q;

    std::vector<int> received;  // consumer-owned; main reads after join
    received.reserve(kN);

    std::thread consumer([&] {
        int v;
        while (static_cast<int>(received.size()) < kN) {
            if (q.pop(v))
                received.push_back(v);
            else
                mdp::cpu_pause();
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < kN; ++i)
            while (!q.push(i))
                mdp::cpu_pause();
    });

    producer.join();
    consumer.join();

    CHECK(static_cast<int>(received.size()) == kN);

    int64_t sum = 0;
    bool ordered = true;
    for (int i = 0; i < kN; ++i) {
        sum += received[static_cast<std::size_t>(i)];
        if (received[static_cast<std::size_t>(i)] != i) { ordered = false; break; }
    }
    CHECK(sum == expected_sum(kN));
    CHECK(ordered);
}

// ── 7. RawMessage round-trip ──────────────────────────────────────────────────
static void test_rawmessage_roundtrip() {
    mdp::SPSCQueue<mdp::RawMessage, 16> q;

    mdp::RawMessage m{};
    m.type         = mdp::MsgType::Add;
    m.venue_id     = 42;
    m.order_id     = 12345678;
    m.timestamp_ns = 9'999'999'999ULL;
    m.price_raw    = 10'050;
    m.quantity     = 500;
    m.side         = 'B';

    CHECK(q.push(m));

    mdp::RawMessage out{};
    CHECK(q.pop(out));
    CHECK(out.type         == mdp::MsgType::Add);
    CHECK(out.venue_id     == 42u);
    CHECK(out.order_id     == 12345678u);
    CHECK(out.timestamp_ns == 9'999'999'999ULL);
    CHECK(out.price_raw    == 10'050);
    CHECK(out.quantity     == 500u);
    CHECK(out.side         == 'B');
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    std::puts("Running SPSC queue tests...\n");

    test_empty_and_full();
    test_fifo_order();
    test_pop_empty();
    test_wraparound_crossing();
    test_fill_drain_repeat();
    test_rawmessage_roundtrip();

    std::puts("Running concurrent correctness test (1M messages)...");
    test_concurrent_correctness();

    std::printf("\n%s  (%d passed, %d failed)\n",
                g_fail == 0 ? "ALL PASS" : "FAILURES DETECTED",
                g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
