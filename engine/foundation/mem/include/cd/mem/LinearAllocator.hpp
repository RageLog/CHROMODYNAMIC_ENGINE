// =============================================================================
// CHROMODYNAMIC — cd/mem/LinearAllocator.hpp
// ADR-005 §D + ADR-017 P0 (DfH ArenaAllocator)
//
// Bump-pointer allocator. Per-frame transient memory; reset() at frame
// boundary. Zero-cost deallocate (no-op). Not thread-safe; concurrent variant
// lands in S2.1.c with cd::concurrency.
// =============================================================================
#pragma once

#include <cd/mem/IAllocator.hpp>

#include <cstdint>

namespace cd::mem
{

class LinearAllocator final : public IAllocator
{
public:
    /// Construct with a fixed-capacity backing block allocated from `backing`.
    /// If `backing == nullptr`, the system_allocator() is used.
    explicit LinearAllocator(std::size_t total_size, IAllocator* backing = nullptr) noexcept
        : backing_ { backing != nullptr ? backing : &system_allocator() }
        , capacity_ { total_size }
        , block_ { static_cast<std::byte*>(backing_->allocate(total_size, kDefaultAlignment)) }
    {
    }

    ~LinearAllocator() override
    {
        if (block_ != nullptr)
        {
            backing_->deallocate(block_);
        }
    }

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment) noexcept override
    {
        if (size == 0 || block_ == nullptr)
        {
            return nullptr;
        }
        const std::size_t align = alignment == 0 ? kDefaultAlignment : alignment;
        if (!detail::is_power_of_two(align))
        {
            return nullptr;
        }
        // size_t == uintptr_t on every target we ship (Win/Linux/macOS x86-64,
        // ARM64). Use size_t throughout the bump-pointer math so there is no
        // useless cast — GCC -Wuseless-cast flags one even when value-preserving.
        const std::size_t base = reinterpret_cast<std::size_t>(block_) + offset_;
        const std::size_t aligned = detail::align_up(base, align);
        const std::size_t pad = aligned - base;
        if (offset_ + pad + size > capacity_)
        {
            return nullptr;
        }
        offset_ += pad + size;
        return reinterpret_cast<void*>(aligned);
    }

    /// Bump allocators do not free individual blocks — call reset() instead.
    /// This is intentionally a no-op so cd::mem::IAllocator users don't crash.
    void deallocate(void* /*ptr*/) noexcept override
    { /* no-op */
    }

    /// Reclaim the entire arena in O(1). Invalidates every prior allocation.
    void reset() noexcept
    {
        offset_ = 0;
    }

    [[nodiscard]] std::size_t bytes_in_use() const noexcept override
    {
        return offset_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept override
    {
        return capacity_;
    }

    [[nodiscard]] std::size_t bytes_remaining() const noexcept
    {
        return capacity_ - offset_;
    }

private:
    IAllocator* backing_;
    std::size_t capacity_;
    std::byte* block_ { nullptr };
    std::size_t offset_ { 0 };
};

}  // namespace cd::mem
