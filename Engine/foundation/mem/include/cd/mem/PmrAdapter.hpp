// =============================================================================
// CHROMODYNAMIC — cd/mem/PmrAdapter.hpp
// ADR-005 §D — std::pmr bridge so cd::mem allocators feed STL containers.
// =============================================================================
#pragma once

#include <cd/mem/IAllocator.hpp>

#include <memory_resource>

namespace cd::mem
{

/// std::pmr::memory_resource that delegates to a cd::mem::IAllocator. Useful
/// when an engine subsystem wants to feed STL containers with a custom
/// allocator without committing to a specific allocator type.
class PmrAdapter final : public std::pmr::memory_resource
{
public:
    explicit PmrAdapter(IAllocator& inner) noexcept
        : inner_ { &inner }
    {
    }

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* p = inner_->allocate(bytes, alignment);
        if (p == nullptr)
        {
            throw std::bad_alloc {};  // pmr contract requires throwing on failure
        }
        return p;
    }

    void do_deallocate(void* p, std::size_t /*bytes*/, std::size_t /*alignment*/) override
    {
        inner_->deallocate(p);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        const auto* o = dynamic_cast<const PmrAdapter*>(&other);
        return o != nullptr && o->inner_ == inner_;
    }

private:
    IAllocator* inner_;
};

}  // namespace cd::mem
