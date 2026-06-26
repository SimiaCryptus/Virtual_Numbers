# `memo.hpp` — Explicit, Bounded LRU Memoization

## Overview

`include/nam/memo.hpp` implements **explicit, bounded memoization** for
digit-producing sources. It corresponds to Phase 2 of the project plan and
to the THEORY.md section *"Memoization and Structural Sharing."* The file
provides two components:

- `LruDigitCache` — a bounded least-recently-used (LRU) cache mapping a
  digit index to its computed digit value.
- `CachedDigitSource<Producer>` — a wrapper that adapts any sequential
  digit producer into a memoizing source backed by an `LruDigitCache`.

## Design Philosophy

### Explicit, not implicit

Memoization here is an **explicit wrapper**, never an implicit global cache.
This is a deliberate design constraint: implicit caching would break the
value semantics of `fork`. A globally mutable cache shared across forked
values would create aliasing of mutable state and violate referential
transparency, since two logically independent values could observe each
other's computation side effects.

By making the cache an object that is *owned* by each `CachedDigitSource`,
the value semantics are preserved:

- There is **no global mutable state**.
- Each `LruDigitCache` owns its own bounded storage.
- Copying or forking a value does not silently share a cache unless the
  programmer explicitly arranges it.

This mirrors the broader principle in functional and persistent data
structure design (see Okasaki, *Purely Functional Data Structures*, 1998)
that sharing must be **structural** and explicit rather than incidental
through hidden caches.

### Bounded resource usage

The cache is **bounded** to a fixed `capacity` (the user-facing
`.cached(max_digits=N)` mode). This makes memory usage predictable and
prevents unbounded growth when an arbitrarily long digit stream is
queried. Bounding is essential for streaming or potentially infinite
computations, where an unbounded memo table would constitute a space leak.

## Background and References

### Memoization

Memoization — caching the results of deterministic computations keyed by
their arguments — was named by Donald Michie ("Memo Functions and Machine
Learning," *Nature*, 1968). The technique is a cornerstone of dynamic
programming and lazy evaluation. In purely functional settings, memoization
is sound precisely because the underlying function is **referentially
transparent**: the result for a given index never changes, so caching it is
observationally equivalent to recomputing it.

Here the cached function is "the digit at index *i*" of a fixed numeric
stream. Because the stream is deterministic, caching a committed digit is
always safe.

### Least-Recently-Used (LRU) eviction

When a cache must be bounded, an eviction policy decides which entry to
discard on overflow. **LRU** evicts the entry that has gone unused for the
longest time, under the *temporal locality* assumption that recently
accessed items are most likely to be accessed again (Denning, "The Working
Set Model for Program Behavior," *CACM*, 1968). LRU has well-studied
competitive properties in the online-algorithms literature (Sleator and
Tarjan, "Amortized Efficiency of List Update and Paging Rules," *CACM*,
1985).

### The classic LRU implementation

The standard *O(1)* LRU implementation combines:

1. A **hash map** from key to a node, for constant-time lookup.
2. A **doubly linked list** ordering nodes by recency, for constant-time
   promotion (move-to-front) and eviction (pop-from-back).

This idiom is widely documented (e.g., in competitive-programming and
systems texts, and as LeetCode's "LRU Cache" problem). `LruDigitCache`
follows exactly this pattern:

- `std::unordered_map<uint64_t, std::pair<uint32_t, list::iterator>> map_`
  provides the key → (value, position) lookup.
- `std::list<uint64_t> order_` maintains recency order, with the **front**
  as most-recently-used (MRU) and the **back** as least-recently-used (LRU).
- `std::list::splice` performs the move-to-front promotion in *O(1)* without
  invalidating iterators, which is precisely why a stored
  `list::iterator` remains valid across promotions.

## API Reference

### `class LruDigitCache`

A bounded LRU cache from digit index (`uint64_t`) to digit value
(`uint32_t`). It is intentionally a small, intrusive LRU rather than a
heavyweight external dependency.

| Member                                        | Description                                                                      |
|-----------------------------------------------|----------------------------------------------------------------------------------|
| `explicit LruDigitCache(size_t capacity)`     | Construct a cache holding at most `capacity` entries.                            |
| `std::optional<uint32_t> get(uint64_t index)` | Return the cached digit at `index`, promoting it to MRU; `std::nullopt` on miss. |
| `void put(uint64_t index, uint32_t digit)`    | Insert/update a digit; evicts the LRU entry on overflow.                         |
| `size_t size() const`                         | Current number of cached entries.                                                |
| `size_t capacity() const`                     | Configured maximum capacity.                                                     |

**Complexity:** `get` and `put` are amortized *O(1)*, relying on
`unordered_map` lookup and `list::splice`.

**Eviction:** On `put` of a new key when `size() >= capacity`, the entry at
`order_.back()` (the LRU entry) is removed before insertion.

### `template <typename Producer> class CachedDigitSource`

Wraps any callable `Producer` that produces digits **in sequential order**.
The producer is a nullary callable returning `std::optional<uint32_t>`,
where `std::nullopt` represents an *honest pending* result (the next digit
is not yet available).

| Member                                                    | Description                                                                                                        |
|-----------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| `CachedDigitSource(Producer producer, size_t max_digits)` | Construct, sizing the underlying cache to `max_digits`.                                                            |
| `std::optional<uint32_t> digit(uint64_t index)`           | Return the digit at `index`, computing forward as needed and memoizing; `std::nullopt` propagates a pending state. |
| `const LruDigitCache& cache() const`                      | Read-only access to the underlying cache (e.g., for introspection/testing).                                        |

#### Sequential production semantics

The underlying generator is inherently **sequential**: it yields digit 0,
then digit 1, and so on. `CachedDigitSource` tracks how many digits have
been produced in `produced_`. A call to `digit(index)`:

1. Returns immediately on a cache hit (promoting the entry to MRU).
2. Otherwise drives the producer forward from `produced_` up to and
   including `index`, memoizing each result.
3. Returns `std::nullopt` if the producer reports pending mid-way; this
   honestly propagates the pending state rather than fabricating a digit.

This decouples *production order* (strictly sequential) from *query order*
(arbitrary, served from the cache for any already-computed index).

## Honest Pending Propagation

The use of `std::optional` to represent a *pending* digit reflects a
recurring theme in this codebase: computations may be unable to commit to a
result yet (analogous to the table-maker's dilemma in correctly-rounded
decimal output, and to lazy/incremental evaluation generally). Rather than
blocking or guessing, the source returns `std::nullopt`, allowing the caller
to decide how to proceed. This preserves correctness: a digit is only ever
cached once it has been genuinely committed by the producer.

## Usage Notes and Caveats

- **Owned, not shared:** to share a cache between values you must do so
  explicitly; copying a `CachedDigitSource` copies its cache.
- **Sequential producer required:** `Producer` must yield digits in index
  order; out-of-order producers would violate the indexing invariant.
- **Capacity tuning:** choosing `max_digits` trades memory for hit rate.
  A capacity smaller than the working set of repeatedly queried prefixes
  will cause thrashing under LRU eviction.

## References

- D. Michie, "Memo Functions and Machine Learning," *Nature*, 218, 1968.
- C. Okasaki, *Purely Functional Data Structures*, Cambridge Univ. Press,

1998.

- P. J. Denning, "The Working Set Model for Program Behavior," *CACM*,
  11(5), 1968.
- D. D. Sleator and R. E. Tarjan, "Amortized Efficiency of List Update and
  Paging Rules," *CACM*, 28(2), 1985.
- Project `THEORY.md`, section *"Memoization and Structural Sharing."*