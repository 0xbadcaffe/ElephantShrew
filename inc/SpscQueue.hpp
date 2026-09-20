#ifndef ELEPHANTSHREW_SPSC_QUEUE_HPP
#define ELEPHANTSHREW_SPSC_QUEUE_HPP
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>
namespace ElephantShrew {
// Exactly one producer and one consumer. All allocation happens at construction.
template<class T> class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    std::vector<T> slots_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t Next(std::size_t i) const noexcept {
        return i + 1 == slots_.size() ? 0 : i + 1;
    }
public:
    explicit SpscQueue(std::size_t capacity) {
        if (capacity == 0 || capacity == std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("invalid SPSC capacity");
        slots_.resize(capacity + 1); // One sentinel slot distinguishes full/empty.
    }
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    bool TryPush(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = Next(head);
        if (next == tail_.load(std::memory_order_acquire)) return false;
        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool TryPop(T& value) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        value = slots_[tail];
        tail_.store(Next(tail), std::memory_order_release);
        return true;
    }
};
}
#endif
