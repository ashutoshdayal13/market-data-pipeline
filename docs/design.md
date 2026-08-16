# Design Notes

## Why SPSC instead of a general multi-producer queue?

The pipeline has exactly one producer thread and one consumer thread. That
constraint means each queue index has a single writer — the producer owns
`tail`, the consumer owns `head` — so neither thread ever competes for a
slot and no compare-and-swap (CAS) is needed anywhere. A multi-producer
queue would need CAS on `tail` to arbitrate between producers, paying a
retry loop under contention. When the structure of the problem guarantees
one producer, SPSC is the correct choice, not an optimization trick.

## Why acquire/release and not seq_cst?

The only cross-thread requirement is: the consumer must see the contents of
a slot before it sees the index that publishes that slot. A release-store
on `tail` (after writing the slot) paired with an acquire-load of `tail`
(before reading the slot) gives exactly that happens-before edge. The same
pairing protects `head` for the producer's full-check. `seq_cst` would add
a global total order over all atomic operations — a stronger guarantee this
queue does not need, at real cost on ARM and under contention.

## Why is the capacity a power of two?

Index wrapping becomes a bitmask (`index & (Capacity-1)`) instead of a
modulo. One slot is deliberately left unused so that `head == tail` always
means empty and `(tail+1) == head` always means full — without the spare
slot the two states would be indistinguishable.

## Why cache-line separation?

`tail` and `head` sit in separate 64-byte-aligned structs. If they shared a
cache line, every producer store to `tail` would invalidate the line in the
consumer's L1 cache and vice versa — "false sharing" — turning every
operation into cross-core traffic. Each side additionally keeps a local
snapshot of the other index (`cached_head` / `cached_tail`) and only
re-reads the remote atomic when the snapshot indicates full/empty, cutting
remote cache-line reads to near zero in steady state.

## Why spin instead of block on full/empty?

A mutex/condvar hands the core back to the OS scheduler; waking up again
costs microseconds and arrives with jitter. On a latency-sensitive path a
bounded spin with a `pause` hint keeps the thread hot and the worst case
predictable. The spin policy also preserves every message in FIFO order —
nothing is dropped or overwritten. The queue itself stays policy-neutral:
`push`/`pop` just return `false`, and the caller decides what to do.

## Why fixed-point prices?

Floating-point results can differ across compilers and architectures
(rounding modes, contraction). Prices as scaled integers (`int64_t`, 4
decimal places) are deterministic, exact, and directly comparable — the
right representation for anything downstream that must reproduce results.

## Why a virtual Strategy interface?

Virtual dispatch costs a few nanoseconds per call but allows swapping
strategies at runtime without recompiling — useful for comparing a NoOp
baseline against a real strategy in the same binary. Latency is recorded in
`FeedHandler`, not in any strategy, so the same measurement applies no
matter what strategy is wired in. (In production with compile-time-known
strategies, a template/CRTP design would remove the vtable indirection.)

## Why is there no I/O or allocation on the hot path?

A single `printf` or heap allocation can cost more than the entire
per-message budget and shows up as latency outliers. The ring is allocated
once inside the queue object; messages are plain structs copied by value;
all printing happens in `main()` after both threads have joined.

## Latency probe definition

`t0` is stamped by the producer immediately before `push`; `t1` is taken by
the consumer after `normalize`, immediately before the strategy callback.
So a sample includes queue transit (plus any time waiting in the ring) and
normalization — and excludes message construction and strategy work. If
`t0 > t1` (clock anomaly), the sample is dropped so an unsigned wrap cannot
poison the histogram.
