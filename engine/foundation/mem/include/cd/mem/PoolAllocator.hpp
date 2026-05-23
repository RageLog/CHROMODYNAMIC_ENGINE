// =============================================================================
// CHROMODYNAMIC — cd/mem/PoolAllocator.hpp
// ADR-005 §D + ADR-017 P0 (DfH PoolAllocator)
//
// Fixed-block free-list pool. O(1) allocate/deallocate. Not thread-safe in
// v1 (DfH had a mutex + atomic head; we strip those and rely on external
// synchronization. The lock-free variant lands in S2.1.c).
//
// Two flavours:
//   - PoolAllocator       — type-erased, given block_size + capacity at ctor.
//   - TypedPoolAllocator<T> — sizeof(T)-aware sugar around PoolAllocator.
// =============================================================================
#pragma once

#include <cd/mem/IAllocator.hpp>

#include <algorithm>
#include <cstdint>
#include <new>

namespace cd::mem
{

class PoolAllocator final : public IAllocator
{
public:
    /// Construct with a fixed number of fixed-size blocks. `block_size` must be
    /// at least sizeof(void*) (the free-list link). `block_alignment` defaults
    /// to alignof(std::max_align_t) but can be tightened (must be power of 2).
    PoolAllocator(
        std::size_t block_size,
        std::size_t block_count,
        std::size_t block_alignment = kDefaultAlignment,
        IAllocator* backing = nullptr
    ) noexcept
        : backing_ { backing != nullptr ? backing : &system_allocator() }
        , block_size_ { (std::max)(block_size, sizeof(FreeNode)) }
        , block_align_ { block_alignment == 0 ? kDefaultAlignment : block_alignment }
        , block_count_ { block_count }
    {
        if (!detail::is_power_of_two(block_align_) || block_count_ == 0)
        {
            return;
        }
        // Round block size up to alignment for safe pointer arithmetic.
        block_size_ = detail::align_up(block_size_, block_align_);
        const std::size_t total = block_size_ * block_count_;
        storage_ = static_cast<std::byte*>(backing_->allocate(total, block_align_));
        if (storage_ == nullptr)
        {
            return;
        }
        // Build the initial free list. Each block holds either a live user object
        // (when allocated) or a FreeNode link (while free). Placement-new
        // constructs FreeNode objects at well-defined addresses so the chain
        // accesses below are NOT type-punning under the strict aliasing rules.
        // When a block is later handed to a caller, the caller starts a new
        // object lifetime in that storage; when deallocate() returns the block,
        // we placement-new a fresh FreeNode over it again.
        auto* nodes = new (storage_) FreeNode {};  // first node
        free_head_ = nodes;
        auto* prev = nodes;
        for (std::size_t i = 1; i < block_count_; ++i)
        {
            auto* next = new (storage_ + i * block_size_) FreeNode {};
            prev->next = next;
            prev = next;
        }
        prev->next = nullptr;
    }

    ~PoolAllocator() override
    {
        if (storage_ != nullptr)
        {
            backing_->deallocate(storage_);
        }
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// Allocate one block. `size` must be <= block_size; `alignment` must be
    /// <= block_alignment. Otherwise nullptr is returned.
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) noexcept override
    {
        if (size > block_size_ || alignment > block_align_ || free_head_ == nullptr)
        {
            return nullptr;
        }
        auto* node = free_head_;
        free_head_ = node->next;
        ++in_use_;
        return static_cast<void*>(node);
    }

    void deallocate(void* ptr) noexcept override
    {
        if (ptr == nullptr)
        {
            return;
        }
        // The slot previously held a user object whose lifetime ended before the
        // caller returned the block to us. Start a new FreeNode lifetime in the
        // storage so the strict-aliasing rules accept the subsequent ->next read.
        auto* node = new (ptr) FreeNode {};
        node->next = free_head_;
        free_head_ = node;
        if (in_use_ > 0)
        {
            --in_use_;
        }
    }

    [[nodiscard]] std::size_t bytes_in_use() const noexcept override
    {
        return in_use_ * block_size_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept override
    {
        return block_count_ * block_size_;
    }

    [[nodiscard]] std::size_t block_size() const noexcept
    {
        return block_size_;
    }

    [[nodiscard]] std::size_t block_count() const noexcept
    {
        return block_count_;
    }

    [[nodiscard]] std::size_t blocks_in_use() const noexcept
    {
        return in_use_;
    }

    [[nodiscard]] std::size_t blocks_free() const noexcept
    {
        return block_count_ - in_use_;
    }

private:
    struct FreeNode
    {
        FreeNode* next;
    };

    IAllocator* backing_;
    std::size_t block_size_;
    std::size_t block_align_;
    std::size_t block_count_;
    std::byte* storage_ { nullptr };
    FreeNode* free_head_ { nullptr };
    std::size_t in_use_ { 0 };
};

/// Typed sugar — sizeof(T)-aware. `Create(args...)` runs T's constructor;
/// `Destroy(p)` invokes its destructor before returning the slot. Same
/// thread-safety story as PoolAllocator (single-thread v1).
template <class T>
class TypedPoolAllocator
{
public:
    explicit TypedPoolAllocator(std::size_t object_count, IAllocator* backing = nullptr) noexcept
        : pool_ { sizeof(T), object_count, alignof(T), backing }
    {
    }

    template <class... Args>
    [[nodiscard]] T* create(Args&&... args)
    {
        void* mem = pool_.allocate(sizeof(T), alignof(T));
        if (mem == nullptr)
        {
            return nullptr;
        }
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    void destroy(T* p) noexcept
    {
        if (p == nullptr)
        {
            return;
        }
        p->~T();
        pool_.deallocate(p);
    }

    [[nodiscard]] std::size_t in_use() const noexcept
    {
        return pool_.blocks_in_use();
    }

    [[nodiscard]] std::size_t capacity_objects() const noexcept
    {
        return pool_.block_count();
    }

private:
    PoolAllocator pool_;
};

}  // namespace cd::mem
