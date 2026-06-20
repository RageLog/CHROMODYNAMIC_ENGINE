// =============================================================================
// CHROMODYNAMIC — cd/core/SmallVector.hpp
// Phase 21.B / Wave 184 — small-buffer-optimized vector.
//
// Holds up to N elements inline (no heap allocation) and grows on the
// heap when capacity exceeds N. Common pattern for ECS query result
// lists, render-pass attachment arrays, and "usually 4 but occasionally
// 32" use cases that std::vector + reserve doesn't capture cleanly.
//
// Trade-offs vs std::vector:
//   * Avoids heap allocation for size <= N (cache-friendly).
//   * sizeof grows with N (don't pick N=1024).
//   * Move/swap is non-trivial when one side is inline and the other
//     heap-allocated; we re-copy in the constructor path to keep the
//     header-only impl simple.
//
// Surface: push_back / emplace_back / pop_back / insert / erase / reserve /
// front / back / size / capacity / clear / operator[] / iterators.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace cd::core
{

template <class T, std::size_t N>
class SmallVector
{
public:
    SmallVector() noexcept = default;

    SmallVector(const SmallVector& other) { copy_from(other); }
    SmallVector& operator=(const SmallVector& other)
    {
        if (this != &other) { clear(); copy_from(other); }
        return *this;
    }
    SmallVector(SmallVector&& other) noexcept { move_from(std::move(other)); }
    SmallVector& operator=(SmallVector&& other) noexcept
    {
        if (this != &other) { clear(); move_from(std::move(other)); }
        return *this;
    }
    ~SmallVector() { clear(); }

    void push_back(const T& v)
    {
        ensure_capacity(size_ + 1);
        new (data() + size_) T(v);
        ++size_;
    }
    void push_back(T&& v)
    {
        ensure_capacity(size_ + 1);
        new (data() + size_) T(std::move(v));
        ++size_;
    }
    template <class... Args>
    T& emplace_back(Args&&... args)
    {
        ensure_capacity(size_ + 1);
        T* slot = data() + size_;
        new (slot) T(std::forward<Args>(args)...);
        ++size_;
        return *slot;
    }
    void pop_back() noexcept
    {
        if (size_ == 0) return;
        --size_;
        (data() + size_)->~T();
    }

    /// Insert `v` before position `pos` (0..size). Returns a pointer to the
    /// newly-inserted element. Inserting at `size()` is equivalent to
    /// push_back. Out-of-range `pos` is clamped to `size()`.
    T* insert(std::size_t pos, const T& v)
    {
        return insert_impl(pos, v);
    }
    T* insert(std::size_t pos, T&& v)
    {
        return insert_impl(pos, std::move(v));
    }

    /// Erase the element at `pos`, shifting the tail down by one. No-op when
    /// `pos >= size()`. Returns a pointer to the element that now occupies
    /// `pos` (== end() when the last element was erased).
    T* erase(std::size_t pos) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if (pos >= size_) return end();
        T* buf = data();
        for (std::size_t i = pos; i + 1 < size_; ++i)
            buf[i] = std::move(buf[i + 1]);
        --size_;
        (buf + size_)->~T();
        return data() + pos;
    }

    /// Pre-reserve heap capacity. Has no effect if `n <= capacity()`.
    void reserve(std::size_t n)
    {
        ensure_capacity(n);
    }

    [[nodiscard]] T&       front() noexcept       { return data()[0]; }
    [[nodiscard]] const T& front() const noexcept { return data()[0]; }
    [[nodiscard]] T&       back() noexcept         { return data()[size_ - 1]; }
    [[nodiscard]] const T& back() const noexcept   { return data()[size_ - 1]; }
    void clear() noexcept
    {
        for (std::size_t i = 0; i < size_; ++i)
            (data() + i)->~T();
        size_ = 0;
        if (heap_)
        {
            ::operator delete(heap_);
            heap_ = nullptr;
            heap_cap_ = 0;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return heap_ ? heap_cap_ : N;
    }
    [[nodiscard]] T*       data() noexcept       { return heap_ ? heap_ : reinterpret_cast<T*>(inline_.data()); }
    [[nodiscard]] const T* data() const noexcept { return heap_ ? heap_ : reinterpret_cast<const T*>(inline_.data()); }
    [[nodiscard]] T& operator[](std::size_t i) noexcept { return data()[i]; }
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return data()[i]; }

    [[nodiscard]] T*       begin() noexcept       { return data(); }
    [[nodiscard]] T*       end() noexcept         { return data() + size_; }
    [[nodiscard]] const T* begin() const noexcept { return data(); }
    [[nodiscard]] const T* end() const noexcept   { return data() + size_; }

private:
    using Storage = std::array<std::byte, sizeof(T) * N>;
    alignas(T) Storage inline_ {};
    T* heap_ { nullptr };
    std::size_t heap_cap_ { 0 };
    std::size_t size_ { 0 };

    void ensure_capacity(std::size_t need)
    {
        if (need <= capacity()) return;
        const std::size_t new_cap = std::max(need, capacity() * 2);
        T* new_buf = static_cast<T*>(::operator new(sizeof(T) * new_cap));
        for (std::size_t i = 0; i < size_; ++i)
        {
            new (new_buf + i) T(std::move(data()[i]));
            (data() + i)->~T();
        }
        if (heap_) ::operator delete(heap_);
        heap_ = new_buf;
        heap_cap_ = new_cap;
    }

    template <class U>
    T* insert_impl(std::size_t pos, U&& v)
    {
        pos = std::min(pos, size_);
        if (pos == size_)
        {
            push_back(std::forward<U>(v));
            return data() + (size_ - 1);
        }
        ensure_capacity(size_ + 1);  // may reallocate; index `pos` stays valid
        T* buf = data();
        // The current last element moves into the fresh uninitialized slot.
        new (buf + size_) T(std::move(buf[size_ - 1]));
        // Shift the rest of the tail up by one via move-assignment.
        for (std::size_t i = size_ - 1; i > pos; --i)
            buf[i] = std::move(buf[i - 1]);
        // Overwrite the now-duplicated slot at `pos` with the new value.
        buf[pos] = std::forward<U>(v);
        ++size_;
        return buf + pos;
    }

    void copy_from(const SmallVector& o)
    {
        for (std::size_t i = 0; i < o.size_; ++i) push_back(o[i]);
    }
    void move_from(SmallVector&& o) noexcept
    {
        if (o.heap_ != nullptr)
        {
            // Source spilled to the heap: steal the buffer outright. Copying
            // o.size_ (> N) elements into our N-slot inline buffer would
            // overflow it — pointer-steal is both correct and O(1).
            heap_ = o.heap_;
            heap_cap_ = o.heap_cap_;
            size_ = o.size_;
            o.heap_ = nullptr;
            o.heap_cap_ = 0;
            o.size_ = 0;
        }
        else
        {
            // Source is inline (o.size_ <= N): element-wise move into our
            // inline buffer, then destroy the moved-from originals.
            for (std::size_t i = 0; i < o.size_; ++i)
                new (data() + i) T(std::move(o[i]));
            size_ = o.size_;
            o.clear();
        }
    }
};

}  // namespace cd::core
