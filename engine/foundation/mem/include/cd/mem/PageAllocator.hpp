// =============================================================================
// CHROMODYNAMIC — cd/mem/PageAllocator.hpp
// ADR-005 §D + ADR-017 P0 (DfH PageAllocator)
//
// Allocates directly from the OS at page granularity. Used by RHI staging,
// large GPU upload buffers, and as the backing store for arenas/pools that
// want to bypass the C runtime heap.
// =============================================================================
#pragma once

#include <cd/mem/IAllocator.hpp>

#include <cstddef>
#include <memory>

namespace cd::mem
{

class PageAllocator final : public IAllocator
{
public:
    PageAllocator() noexcept;
    ~PageAllocator() override;

    /// `size` is rounded up to a multiple of the OS page size. `alignment` must
    /// be <= page size (the OS guarantees page-aligned returns). Returns
    /// nullptr on failure.
    [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment) noexcept override;

    void deallocate(void* ptr) noexcept override;

    /// Cached page size (queried lazily). Always a power of two.
    [[nodiscard]] static std::size_t page_size() noexcept;

    /// Rounded mapping length recorded for `ptr`, or 0 if `ptr` is not a live
    /// mapping owned by this allocator. POSIX munmap() needs this length; the
    /// accessor also lets tests verify the side-table is balanced (no leak /
    /// double-free) without a platform-specific probe.
    [[nodiscard]] std::size_t mapping_length(const void* ptr) const noexcept;

    /// Number of live mappings currently tracked by this allocator. A correct
    /// allocate/deallocate sequence drives this back to 0.
    [[nodiscard]] std::size_t live_mapping_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::mem
