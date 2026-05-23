// =============================================================================
// CHROMODYNAMIC — cd/concurrency/RingBuffer.hpp
// ADR-005 §G + ADR-015 + ADR-017 P0 (DfH common/utility/ringbuffer.hpp)
//
// Single-producer / single-consumer (SPSC) lock-free ring buffer.
//
// Improvements over DfH baseline:
//   - Cache-line padded head/tail to avoid false sharing.
//   - Power-of-two capacity fast path (bitmask instead of modulo).
//   - Perfect-forwarding `push()` and `try_emplace()` for move-only types.
//   - `consume_all(callable)` with no allocation (no std::vector hop).
//
// Concurrency contract:
//   - Exactly ONE producer thread may call push/try_emplace.
//   - Exactly ONE consumer thread may call pop/consume_all.
//   - Status queries (`empty`, `read_available`, `write_available`) may be
//     called from any thread but are inherently fuzzy.
//
// `reset()` MUST only be called when no thread is concurrently producing or
// consuming.
// =============================================================================
#pragma once

#include <cd/concurrency/Atomics.hpp>
#include <cd/core/Defines.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace cd::concurrency
{

namespace detail
{

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace detail

template <class T, std::size_t Capacity = 4096>
class RingBuffer
{
    static_assert(Capacity >= 2, "RingBuffer Capacity must be at least 2");

public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type kCapacity = Capacity;
    static constexpr size_type kSlots = Capacity + 1;
    static constexpr bool kIsPow2Slots = detail::is_power_of_two(kSlots);
    static constexpr size_type kSlotMask = kIsPow2Slots ? (kSlots - 1) : 0;

    RingBuffer() noexcept = default;
    ~RingBuffer() = default;

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /// Producer side — copy.
    [[nodiscard]] bool push(const T& item)
    {
        return emplace_impl(item);
    }

    /// Producer side — move (zero-copy for move-only T).
    [[nodiscard]] bool push(T&& item)
    {
        return emplace_impl(std::move(item));
    }

    /// Producer side — perfect-forwarding emplace.
    template <class U>
    [[nodiscard]] bool try_emplace(U&& value)
    {
        return emplace_impl(std::forward<U>(value));
    }

    /// Consumer side — copy/move into out.
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

    /// Consumer side — return-by-optional (move-only-friendly).
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

    /// Consumer side — drain every available item, invoking `fn(T&&)`.
    /// Returns the number of items consumed.
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

    [[nodiscard]] size_type read_available() const noexcept
    {
        const size_type head = head_.load(acquire_order);
        const size_type tail = tail_.load(acquire_order);
        if (head >= tail)
        {
            return head - tail;
        }
        return kSlots - (tail - head);
    }

    [[nodiscard]] size_type write_available() const noexcept
    {
        return Capacity - read_available();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return read_available() == 0;
    }

    /// Drops all items. **Not thread-safe** — only call when no producer/consumer
    /// is active.
    void reset() noexcept
    {
        head_.store(0, release_order);
        tail_.store(0, release_order);
    }

private:
    [[nodiscard]] static CD_FORCE_INLINE size_type increment(size_type idx) noexcept
    {
        if constexpr (kIsPow2Slots)
        {
            return (idx + 1) & kSlotMask;
        }
        else
        {
            return (idx + 1) % kSlots;
        }
    }

    template <class U>
    bool emplace_impl(U&& value)
    {
        const size_type head = head_.load(relaxed_order);
        const size_type next = increment(head);
        if (next == tail_.load(acquire_order))
        {
            return false;  // full
        }
        slots_[head] = std::forward<U>(value);
        head_.store(next, release_order);
        return true;
    }

    // Slots are non-atomic; SPSC invariant guarantees no concurrent slot access.
    std::array<T, kSlots> slots_ {};

    // Cache-line isolate head and tail to avoid false sharing between producer
    // and consumer caches (Naughty Dog / Frostbite pattern; ADR-015 §G).
    alignas(::cd::core::kCacheLineSize) std::atomic<size_type> head_ { 0 };
    alignas(::cd::core::kCacheLineSize) std::atomic<size_type> tail_ { 0 };
};

}  // namespace cd::concurrency
