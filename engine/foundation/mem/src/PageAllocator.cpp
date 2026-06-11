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

namespace cd::mem
{

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
    return ::VirtualAlloc(nullptr, rounded, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* p = ::mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

void PageAllocator::deallocate(void* ptr) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }
#if CD_OS_WINDOWS
    ::VirtualFree(ptr, 0, MEM_RELEASE);
#else
    // POSIX munmap requires the original length; we did not store it. Callers
    // that need true page-granularity reclamation should track lengths
    // externally. For Sprint S2.1.b this is documented as a v1 limitation —
    // PageAllocator on POSIX is intended primarily for long-lived blocks that
    // outlive the process address space anyway.
    // TODO(S2.1.c): record (ptr, length) in a side map for correct munmap.
    (void)ptr;
#endif
}

}  // namespace cd::mem
