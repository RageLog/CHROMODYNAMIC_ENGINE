// =============================================================================
// CHROMODYNAMIC — cd/mem/PageAllocator.cpp
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/mem/PageAllocator.hpp>

#if CD_OS_WINDOWS
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// clang-format on
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace cd::mem
{

// Side-table mapping each live base pointer to the rounded mapping length we
// passed to the OS. POSIX munmap() requires that exact length; Windows
// VirtualFree(MEM_RELEASE) ignores it but we still track so the table stays
// balanced and tests can assert no leak / double-free portably.
struct PageAllocator::Impl
{
    mutable std::mutex mtx;
    std::unordered_map<const void*, std::size_t> lengths;
};

namespace
{
std::atomic<std::size_t> g_cached_page_size { 0 };

std::size_t query_page_size() noexcept
{
#if CD_OS_WINDOWS
    SYSTEM_INFO info {};
    ::GetSystemInfo(&info);
    return static_cast<std::size_t>(info.dwPageSize);
#else
    long sz = ::sysconf(_SC_PAGESIZE);
    return sz > 0 ? static_cast<std::size_t>(sz) : 4096u;
#endif
}
}  // namespace

PageAllocator::PageAllocator() noexcept
    : impl_(std::make_unique<Impl>())
{
}

PageAllocator::~PageAllocator() = default;

std::size_t PageAllocator::page_size() noexcept
{
    std::size_t cached = g_cached_page_size.load(std::memory_order_relaxed);
    if (cached == 0)
    {
        cached = query_page_size();
        g_cached_page_size.store(cached, std::memory_order_relaxed);
    }
    return cached;
}

void* PageAllocator::do_allocate(std::size_t size, std::size_t alignment) noexcept
{
    if (size == 0)
    {
        return nullptr;
    }
    const std::size_t ps = page_size();
    if (alignment > ps)
    {
        // Page allocators do not over-align beyond the page boundary.
        return nullptr;
    }
    const std::size_t rounded = detail::align_up(size, ps);
#if CD_OS_WINDOWS
    void* p = ::VirtualAlloc(nullptr, rounded, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* p = ::mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
    {
        p = nullptr;
    }
#endif
    if (p != nullptr)
    {
        // Record the rounded length so deallocate() can hand POSIX munmap() the
        // exact mapping size. The map insert may throw bad_alloc; if it does the
        // mapping is leaked rather than the process aborting in this noexcept
        // function — acceptable under OOM, which is the only way the small
        // insert fails.
        const std::scoped_lock lock(impl_->mtx);
        impl_->lengths[p] = rounded;
    }
    return p;
}

void PageAllocator::deallocate(void* ptr) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }

    std::size_t rounded = 0;
    {
        const std::scoped_lock lock(impl_->mtx);
        const auto it = impl_->lengths.find(ptr);
        if (it == impl_->lengths.end())
        {
            // Not a live mapping owned by this allocator (double-free / foreign
            // pointer). Refuse to unmap an unknown length.
            return;
        }
        rounded = it->second;
        impl_->lengths.erase(it);
    }

#if CD_OS_WINDOWS
    // VirtualFree(MEM_RELEASE) reclaims the whole reservation; length must be 0.
    (void)rounded;
    ::VirtualFree(ptr, 0, MEM_RELEASE);
#else
    // POSIX munmap requires the original mapping length, now recovered from the
    // side-table above. This releases the address-space reservation correctly
    // instead of leaking it (the former v1 limitation).
    ::munmap(ptr, rounded);
#endif
}

std::size_t PageAllocator::mapping_length(const void* ptr) const noexcept
{
    if (ptr == nullptr)
    {
        return 0;
    }
    const std::scoped_lock lock(impl_->mtx);
    const auto it = impl_->lengths.find(ptr);
    return it == impl_->lengths.end() ? 0U : it->second;
}

std::size_t PageAllocator::live_mapping_count() const noexcept
{
    const std::scoped_lock lock(impl_->mtx);
    return impl_->lengths.size();
}

}  // namespace cd::mem
