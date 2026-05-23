// =============================================================================
// CHROMODYNAMIC — cd/core/RingBuffer.hpp
// Phase 26.A / Wave 194 — fixed-capacity FIFO ring buffer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <new>
#include <optional>
#include <utility>

namespace cd::core
{

template <class T, std::size_t N>
class RingBuffer
{
public:
    RingBuffer() = default;
    ~RingBuffer() { clear(); }

    /// Push at the tail. Returns false if full.
    bool push(const T& v) { return emplace_(v); }
    bool push(T&& v) { return emplace_(std::move(v)); }

    /// Pop the head. Returns std::nullopt when empty.
    std::optional<T> pop() noexcept
    {
        if (size_ == 0) return std::nullopt;
        T* slot = reinterpret_cast<T*>(&buf_[head_ * sizeof(T)]);
        std::optional<T> out { std::move(*slot) };
        slot->~T();
        head_ = (head_ + 1) % N;
        --size_;
        return out;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == N; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    void clear() noexcept
    {
        while (!empty()) (void)pop();
    }

private:
    template <class U>
    bool emplace_(U&& v)
    {
        if (size_ == N) return false;
        T* slot = reinterpret_cast<T*>(&buf_[tail_ * sizeof(T)]);
        new (slot) T(std::forward<U>(v));
        tail_ = (tail_ + 1) % N;
        ++size_;
        return true;
    }

    alignas(T) std::array<std::byte, sizeof(T) * N> buf_ {};
    std::size_t head_ { 0 };
    std::size_t tail_ { 0 };
    std::size_t size_ { 0 };
};

}  // namespace cd::core
