// =============================================================================
// CHROMODYNAMIC — cd/core/PoolAllocator.hpp
// Phase 70.B / Wave 238 — fixed-size block pool allocator.
//
// O(1) allocate/free for homogeneous fixed-size objects. The pool
// owns a contiguous byte array of `block_size * block_count` bytes
// and maintains a free-list threaded through the unused blocks.
//
// Use for ECS sparse-set storage chunks, particle pool, command-list
// scratch slots — anywhere FrameAllocator is too coarse (no per-
// element free) and `new/delete` too contended.
//
// Returns `nullptr` on exhaustion. Caller is responsible for in-place
// construction/destruction (allocator returns raw bytes).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace cd::core
{

class PoolAllocator
{
public:
    PoolAllocator(std::size_t block_size, std::size_t block_count)
        : block_size_ { (block_size < sizeof(void*)) ? sizeof(void*) : block_size },
          block_count_ { block_count },
          storage_ { std::make_unique<std::byte[]>(block_size_ * block_count_) }
    {
        // Build the free list: each free block stores a pointer to the
        // next free block in its first sizeof(void*) bytes.
        for (std::size_t i = 0; i + 1 < block_count_; ++i)
        {
            auto* cur = block_at_(i);
            auto* next = block_at_(i + 1);
            std::memcpy(cur, &next, sizeof(void*));
        }
        void* terminator = nullptr;
        std::memcpy(block_at_(block_count_ - 1), &terminator, sizeof(void*));
        free_head_ = block_at_(0);
        free_count_ = block_count_;
    }

    [[nodiscard]] void* allocate() noexcept
    {
        if (free_head_ == nullptr) return nullptr;
        void* out = free_head_;
        void* next = nullptr;
        std::memcpy(&next, free_head_, sizeof(void*));
        free_head_ = next;
        --free_count_;
        return out;
    }

    void deallocate(void* p) noexcept
    {
        if (p == nullptr) return;
        std::memcpy(p, &free_head_, sizeof(void*));
        free_head_ = p;
        ++free_count_;
    }

    [[nodiscard]] std::size_t block_size()  const noexcept { return block_size_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return block_count_; }
    [[nodiscard]] std::size_t free_count()  const noexcept { return free_count_; }
    [[nodiscard]] std::size_t used_count()  const noexcept { return block_count_ - free_count_; }

private:
    [[nodiscard]] void* block_at_(std::size_t i) noexcept
    {
        return storage_.get() + i * block_size_;
    }

    std::size_t                  block_size_;
    std::size_t                  block_count_;
    std::unique_ptr<std::byte[]> storage_;
    void*                        free_head_  { nullptr };
    std::size_t                  free_count_ { 0 };
};

}  // namespace cd::core
