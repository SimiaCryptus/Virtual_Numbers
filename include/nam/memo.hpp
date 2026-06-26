// include/nam/memo.hpp
//
// Phase 2: Explicit, bounded LRU memoization (THEORY.md "Memoization and
// Structural Sharing"). Memoization is an EXPLICIT wrapper, never an
// implicit global cache -- implicit caching would break the value
// semantics of fork. There is no global mutable state; each LruDigitCache
// owns its own bounded storage.
//
// This caches committed digit prefixes for a digit-producing source so
// repeated prefix queries do not recompute. Sized by max_digits, mapping
// the user-facing ".cached(max_digits=N)" mode.
#ifndef NAM_MEMO_HPP
#define NAM_MEMO_HPP

#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nam
{
    // A bounded LRU cache from digit-index -> digit. Bounded to `capacity`
    // entries; on overflow the least-recently-used entry is evicted. This is
    // a small intrusive LRU, not a heavy external dependency, per the plan.
    class LruDigitCache
    {
    public:
        explicit LruDigitCache(size_t capacity) : capacity_(capacity)
        {
        }

        std::optional<uint32_t> get(uint64_t index)
        {
            auto it = map_.find(index);
            if (it == map_.end()) return std::nullopt;
            // Move to front (most-recently-used).
            order_.splice(order_.begin(), order_, it->second.second);
            return it->second.first;
        }

        void put(uint64_t index, uint32_t digit)
        {
            auto it = map_.find(index);
            if (it != map_.end())
            {
                it->second.first = digit;
                order_.splice(order_.begin(), order_, it->second.second);
                return;
            }
            if (map_.size() >= capacity_ && !order_.empty())
            {
                uint64_t evict = order_.back();
                order_.pop_back();
                map_.erase(evict);
            }
            order_.push_front(index);
            map_[index] = {digit, order_.begin()};
        }

        size_t size() const { return map_.size(); }
        size_t capacity() const { return capacity_; }

    private:
        size_t capacity_;
        std::list<uint64_t> order_; // front = MRU, back = LRU
        std::unordered_map<uint64_t,
                           std::pair<uint32_t, std::list<uint64_t>::iterator>> map_;
    };

    // A cached digit source: wraps any callable producing the digit at a
    // given (sequential) index. The wrapper enforces sequential production
    // (the underlying generator is sequential) but serves cache hits for any
    // already-computed index. `produce_next` must return digits in order.
    template <typename Producer>
    class CachedDigitSource
    {
    public:
        CachedDigitSource(Producer producer, size_t max_digits)
            : producer_(std::move(producer)), cache_(max_digits)
        {
        }

        // Returns the digit at `index`, computing sequentially as needed and
        // memoizing. nullopt propagates an honest pending from the producer.
        std::optional<uint32_t> digit(uint64_t index)
        {
            if (auto c = cache_.get(index)) return c;
            // Compute forward from produced_ up to index.
            while (produced_ <= index)
            {
                auto d = producer_();
                if (!d.has_value()) return std::nullopt; // pending
                cache_.put(produced_, *d);
                ++produced_;
            }
            return cache_.get(index);
        }

        const LruDigitCache& cache() const { return cache_; }

    private:
        Producer producer_;
        LruDigitCache cache_;
        uint64_t produced_ = 0;
    };
} // namespace nam

#endif // NAM_MEMO_HPP