# market-data-pipeline

A low-latency market data processing pipeline in C++20: a producer thread
streams synthetic exchange messages to a consumer thread through a lock-free
single-producer/single-consumer (SPSC) ring buffer, with nanosecond-resolution
latency measurement.

```
FeedGenerator ──push──▶ SPSC ring (65536) ──pop──▶ normalize ──▶ Strategy
  (producer thread)                          (consumer thread)
```

## Build & run

Requires GNU Make and a C++20 compiler (GCC 11+ / Clang 14+).

```bash
make -j          # build everything (Release: -O3 -march=native)
make test        # run unit tests
make run         # run the pipeline (2M messages)
make bench       # run the benchmark suite
make tsan        # run queue tests under ThreadSanitizer (Linux)
make clean       # remove build output
```

For stable latency numbers on Linux, pin each thread to its own core:

```bash
export MDP_PRODUCER_CPU=2 MDP_CONSUMER_CPU=3
taskset -c 2,3 ./build/benchmark
```

## How it works

**Producer thread** (`FeedGenerator`) generates synthetic messages —
50% Add, 20% Cancel, 20% Modify, 10% Trade, with prices in integer cents —
stamps each with a nanosecond timestamp immediately before pushing it into
the queue.

**The queue** (`SPSCQueue`) is a lock-free ring buffer. Because there is
exactly one producer and one consumer, each index has a single writer: the
producer owns `tail`, the consumer owns `head`. That removes the need for
any compare-and-swap — plain atomic stores with acquire/release ordering are
enough:

```
push:  write buf[tail]   →  tail.store(release)     // publish the slot
pop:   tail.load(acquire) →  read buf[head]  →  head.store(release)
```

The release/acquire pair guarantees the consumer sees the slot contents
before it sees the new index. `head` and `tail` live on separate 64-byte
cache lines so the two cores never invalidate each other's line on every
operation (false sharing), and each side keeps a cached snapshot of the
other's index so it rarely reads the remote cache line at all.

Capacity is a power of two (65536), so index wrapping is a single bitmask.
One slot is kept unused so `head == tail` always means empty; the queue
stores at most 65535 messages.

**Backpressure:** when the ring is full, the producer spins with a CPU
`pause` hint until space appears — no messages are dropped, no locks are
taken, and FIFO order is preserved.

**Consumer thread** (`FeedHandler`) pops each message, normalizes it into a
canonical `MarketEvent` (fixed-point price: `int64_t` with 4 decimal places
— integers keep results deterministic across platforms), records the
latency sample, and dispatches to a pluggable `Strategy` callback
(polymorphic base class; `NoOpStrategy` for clean benchmarks,
`StatsStrategy` for event counting).

**No heap, no I/O on the hot path:** the ring is a fixed array allocated
once inside the queue object; messages are plain structs passed by value;
nothing in the producer/consumer loop allocates, locks, or prints. All
output happens after both threads have joined.

## Latency measurement

The probe measures **push → pre-callback**:

```
t0 = now_ns()              ← stamped by producer, immediately before push
   … queue transit (and queue wait, if the ring has a backlog) …
   pop → normalize
t1 = now_ns()              ← taken by consumer, before the strategy callback
latency = t1 − t0
```

Samples go into a 100 ns-resolution histogram (fixed buckets, catch-all
tail) that reports min / mean / p50 / p90 / p99 / max. The histogram is
written only by the consumer thread and read after `join()` — no
synchronization needed. The clock is `CLOCK_MONOTONIC_RAW` on Linux (not
adjusted by NTP, so the tick rate is stable between two nearby readings).

The benchmark reports this probe in two regimes — they answer different
questions:

| Regime | `max_in_flight` | What it measures |
|---|---|---|
| Intrinsic latency | 1 (producer waits for an empty queue before each push) | Pure transit + normalize — hundreds of ns |
| Overload | 0 (full blast; producer outruns consumer) | Queue wait — p50 ≈ time to drain a full ring |

Keeping the two separate matters: under overload, latency is dominated by
time spent waiting in the ring, which says nothing about how fast the
pipeline itself is.

## Benchmark results

| Metric | Value |
|---|---|
| Pipeline throughput (NoOp, full blast) | 13.03 M msg/s |
| Queue push/pop (1 thread) | 304.14 msg/s |
| Queue push/pop (2 threads) | 232.49 msg/s |
| Intrinsic latency p50 / p99 | 200 ns / 300 ns |
| Overload latency p50 | 4778720 ns |

## Tests

`tests/test_spsc_queue.cpp` (no framework dependencies):

- Empty/full semantics, including the full → empty → reuse boundary
- FIFO ordering, pop-on-empty
- Wraparound with head/tail crossing the ring boundary while non-empty
- 10k repeated fill/drain cycles
- `RawMessage` round-trip through the queue
- 1M-message concurrent producer/consumer run verifying size, checksum,
  and total order — proves no loss, duplication, or reordering

`tests/test_pipeline_zero.cpp`: zero-message smoke test (clean shutdown,
no divide-by-zero, no spurious samples).

The queue tests can also run under **ThreadSanitizer** to catch data races:

```bash
make tsan
```

## Design decisions

Short version (details in [`docs/design.md`](docs/design.md)):

- **SPSC, not a general MPMC queue** — one producer and one consumer means
  each index has a single writer, which eliminates CAS entirely.
- **Acquire/release, not seq_cst** — the queue only needs "slot write
  happens-before index read"; a global total order is unnecessary cost.
- **Spin, not sleep** — blocking on a mutex/condvar hands the core to the
  scheduler and adds wakeup jitter; a bounded spin with `pause` keeps
  worst-case latency predictable.
- **Fixed-point prices** — floating point is non-deterministic across
  platforms/compilers; scaled `int64_t` is exact and comparable.
- **Virtual `Strategy` dispatch** — a few ns per call in exchange for
  swapping strategies at runtime; latency is recorded in the pipeline layer
  so every strategy gets the same measurement.

## Layout

```
include/mdp/   public headers (queue, messages, pipeline components)
src/           implementations + pipeline / benchmark executables
tests/         unit + concurrency tests
docs/          design notes
```

## License

MIT — see [LICENSE](LICENSE).
