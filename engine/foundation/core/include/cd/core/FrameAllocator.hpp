// =============================================================================
// CHROMODYNAMIC — cd/core/FrameAllocator.hpp
// Phase 25.A / Wave 192 — header-only linear (arena) allocator.
//
// Bump-pointer allocator backed by a single owned chunk. allocate(size,
// align) returns a raw byte pointer. reset() rewinds the bump pointer
// (objects must be trivially-destructible for this to be safe).
//
// Typical use: per-frame scratch allocations (command-list arguments,
// transient string formatting, ImGui temp buffers) that get nuked at
// frame end. Avoids the heap allocator's contention + per-allocation
// metadata for short-lived data.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cd::core
{

class FrameAllocator
{
public:
    explicit FrameAllocator(std::size_t capacity_bytes)
        : capacity_ { capacity_bytes }
        , storage_ { new std::byte[capacity_bytes] }
    {
    }

    /// Allocate `bytes` of memory aligned to `align`. Returns nullptr
    /// when the request can't fit in the remaining capacity.
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align = alignof(std::max_align_t)) noexcept
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage_.get()) + used_;
        const std::uintptr_t aligned = (base + (align - 1)) & ~static_cast<std::uintptr_t>(align - 1);
        const auto pad = static_cast<std::size_t>(aligned - base);
        if (used_ + pad + bytes > capacity_)
            return nullptr;
        used_ += pad + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    /// Rewind. Pointers returned by previous `allocate()` calls are
    /// invalidated. Trivially-destructible payloads only.
    void reset() noexcept { used_ = 0; }

    /// Rewind to a prior `used()` snapshot. Lets callers reclaim
    /// scratch space at scope granularity finer than a full frame —
    /// pair with `ArenaScope` for RAII bracketing.
    void rewind(std::size_t mark) noexcept
    {
        used_ = std::min(mark, used_);
    }

    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return capacity_ - used_; }

private:
    std::size_t capacity_ { 0 };
    std::size_t used_ { 0 };
    std::unique_ptr<std::byte[]> storage_;
};

}  // namespace cd::core
