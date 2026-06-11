// =============================================================================
// CHROMODYNAMIC — cd/mem/IAllocator.hpp
// ADR-005 §D Memory Allocator + ADR-017 P0 (DtForHil memory/allocator salvage)
//
// Engine-wide allocator interface. All concrete allocators in cd::mem derive
// from this so they can be used interchangeably behind a stable surface.
//
// Notes:
//   - alignment defaults to alignof(std::max_align_t). 0 means "let the
//     allocator pick a sensible default" (alias for default alignment).
//   - Returning nullptr is the failure signal. Implementations may also throw
//     std::bad_alloc for compatibility with std::pmr, but the engine API
//     contract is **nothrow + null on failure**.
//   - deallocate(nullptr) is a no-op (matches std::free).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#if defined(_MSC_VER)
    #include <malloc.h>  // _aligned_malloc / _aligned_free
#endif

namespace cd::mem
{

inline constexpr std::size_t kDefaultAlignment = alignof(std::max_align_t);

class IAllocator
{
public:
    IAllocator() noexcept = default;
    virtual ~IAllocator() = default;

    IAllocator(const IAllocator&) = delete;
    IAllocator& operator=(const IAllocator&) = delete;
    IAllocator(IAllocator&&) = delete;
    IAllocator& operator=(IAllocator&&) = delete;

    /// NVI wrapper — the default argument lives HERE, never on the
    /// virtual (google-default-arguments: defaults on virtuals bind
    /// statically and may silently diverge between overrides).
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = kDefaultAlignment) noexcept
    {
        return do_allocate(size, alignment);
    }

    virtual void deallocate(void* ptr) noexcept = 0;

    /// Optional diagnostic — number of bytes currently outstanding (live).
    /// Default returns 0 (allocators that don't track may opt out).
    [[nodiscard]] virtual std::size_t bytes_in_use() const noexcept
    {
        return 0;
    }

    /// Optional diagnostic — total capacity of the underlying arena/pool/heap.
    /// Returns 0 when capacity is unbounded (e.g., the system allocator).
    [[nodiscard]] virtual std::size_t capacity() const noexcept
    {
        return 0;
    }

protected:
    /// Implementation hook for allocate(). `alignment` is always the
    /// caller-resolved value (the public wrapper applied the default).
    [[nodiscard]] virtual void* do_allocate(std::size_t size, std::size_t alignment) noexcept = 0;
};

/// Trivial system allocator backed by aligned new/delete. Safe to use as a
/// fallback when no custom strategy applies.
class SystemAllocator final : public IAllocator
{
public:
    [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment) noexcept override
    {
        if (size == 0)
        {
            return nullptr;
        }
        const std::size_t align = alignment == 0 ? kDefaultAlignment : alignment;
#if defined(_WIN32)
        // _aligned_malloc is provided by MSVCRT (and ucrt), so MSVC, clang-cl, and
        // MinGW-w64 all use it. POSIX posix_memalign is not available on Windows.
        return ::_aligned_malloc(size, align);
#else
        void* p = nullptr;
        if (::posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, size) != 0)
        {
            return nullptr;
        }
        return p;
#endif
    }

    void deallocate(void* ptr) noexcept override
    {
        if (ptr == nullptr)
        {
            return;
        }
#if defined(_WIN32)
        ::_aligned_free(ptr);
#else
        ::free(ptr);
#endif
    }
};

/// Return a process-wide singleton SystemAllocator. Convenient default for
/// libraries that need *some* allocator without dictating policy.
[[nodiscard]] inline IAllocator& system_allocator() noexcept
{
    static SystemAllocator s_instance;
    return s_instance;
}

// --- Alignment utilities ----------------------------------------------------
namespace detail
{

[[nodiscard]] constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept
{
    return (value + (alignment - 1)) & ~(alignment - 1);
}

[[nodiscard]] inline std::uintptr_t align_up_ptr(std::uintptr_t value, std::size_t alignment) noexcept
{
    // alignment is std::size_t; on 64-bit (size_t == uintptr_t) the explicit
    // cast is useless and trips GCC -Wuseless-cast. Use the size_t literal
    // mask, which the compiler converts at the bitwise op without a cast.
    return (value + (alignment - 1)) & ~(alignment - 1);
}

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace detail
}  // namespace cd::mem
