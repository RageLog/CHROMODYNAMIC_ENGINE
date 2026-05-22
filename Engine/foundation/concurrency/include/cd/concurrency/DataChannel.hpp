// =============================================================================
// CHROMODYNAMIC — cd/concurrency/DataChannel.hpp
// ADR-015 + ADR-017 P0 (DfH common/data/datachannel.hpp)
//
// Named typed SPSC channel. Same SPSC invariants as RingBuffer but with:
//   - Runtime-sized capacity (heap-backed vector)
//   - Stable string name (logging / wiring / debug overlays)
//   - Convenience batched read/write APIs
//
// For compile-time fixed-capacity in hot loops, prefer RingBuffer<T,N>.
// =============================================================================
#pragma once

#include <cd/concurrency/Atomics.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::concurrency
{

template <class T>
class DataChannel
{
public:
    using value_type = T;
    using size_type = std::size_t;

    explicit DataChannel(std::string name, size_type capacity = 4096)
        : name_ { std::move(name) }
        , capacity_ { capacity == 0 ? 1 : capacity }
        , slots_(capacity_ + 1)
    {
    }

    [[nodiscard]] std::string_view name() const noexcept
    {
        return name_;
    }

    // Producer side
    [[nodiscard]] bool push(const T& v)
    {
        return emplace_impl(v);
    }

    [[nodiscard]] bool push(T&& v)
    {
        return emplace_impl(std::move(v));
    }

    template <class U>
    [[nodiscard]] bool try_emplace(U&& v)
    {
        return emplace_impl(std::forward<U>(v));
    }

    // Consumer side
    [[nodiscard]] bool pop(T& out)
    {
        const size_type tail = tail_.load(relaxed_order);
        if (tail == head_.load(acquire_order))
        {
            return false;
        }
        out = std::move(slots_[tail]);
        tail_.store(increment(tail), release_order);
        return true;
    }

    [[nodiscard]] std::optional<T> try_pop()
    {
        const size_type tail = tail_.load(relaxed_order);
        if (tail == head_.load(acquire_order))
        {
            return std::nullopt;
        }
        std::optional<T> result { std::move(slots_[tail]) };
        tail_.store(increment(tail), release_order);
        return result;
    }

    template <class F>
    size_type consume_all(F&& fn)
    {
        size_type count = 0;
        while (true)
        {
            const size_type tail = tail_.load(relaxed_order);
            if (tail == head_.load(acquire_order))
            {
                break;
            }
            fn(std::move(slots_[tail]));
            tail_.store(increment(tail), release_order);
            ++count;
        }
        return count;
    }

    // Status
    [[nodiscard]] size_type read_available() const noexcept
    {
        const size_type head = head_.load(acquire_order);
        const size_type tail = tail_.load(acquire_order);
        if (head >= tail)
        {
            return head - tail;
        }
        return slots_.size() - (tail - head);
    }

    [[nodiscard]] size_type write_available() const noexcept
    {
        return capacity_ - read_available();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return read_available() == 0;
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return read_available();
    }

    [[nodiscard]] size_type capacity() const noexcept
    {
        return capacity_;
    }

private:
    template <class U>
    bool emplace_impl(U&& v)
    {
        const size_type head = head_.load(relaxed_order);
        const size_type next = increment(head);
        if (next == tail_.load(acquire_order))
        {
            return false;
        }
        slots_[head] = std::forward<U>(v);
        head_.store(next, release_order);
        return true;
    }

    [[nodiscard]] CD_FORCE_INLINE size_type increment(size_type idx) const noexcept
    {
        return (idx + 1) % slots_.size();
    }

    std::string name_;
    size_type capacity_;
    std::vector<T> slots_;
    alignas(::cd::core::kCacheLineSize) std::atomic<size_type> head_ { 0 };
    alignas(::cd::core::kCacheLineSize) std::atomic<size_type> tail_ { 0 };
};

}  // namespace cd::concurrency
