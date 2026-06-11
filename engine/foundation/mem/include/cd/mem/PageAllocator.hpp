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

namespace cd::mem
{

class PageAllocator final : public IAllocator
{
public:
    PageAllocator() noexcept = default;
    ~PageAllocator() override = default;

    /// `size` is rounded up to a multiple of the OS page size. `alignment` must
    /// be <= page size (the OS guarantees page-aligned returns). Returns
    /// nullptr on failure.
    [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment) noexcept override;

    void deallocate(void* ptr) noexcept override;

    /// Cached page size (queried lazily). Always a power of two.
    [[nodiscard]] static std::size_t page_size() noexcept;
};

}  // namespace cd::mem
