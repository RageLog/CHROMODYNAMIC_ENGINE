// =============================================================================
// CHROMODYNAMIC — cd/time/HiResClock.cpp
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/time/HiResClock.hpp>

#if CD_OS_WINDOWS
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// clang-format on
#else
    #include <ctime>
#endif

#include <atomic>

namespace cd::time
{

namespace
{

#if CD_OS_WINDOWS
struct QpcFrequency
{
    LARGE_INTEGER freq {};

    QpcFrequency() noexcept
    {
        ::QueryPerformanceFrequency(&freq);
    }

    [[nodiscard]] std::int64_t hz() const noexcept
    {
        return freq.QuadPart;
    }
};

const QpcFrequency& qpc_freq() noexcept
{
    static const QpcFrequency s;
    return s;
}
#endif

}  // namespace

std::uint64_t hires_now_ns() noexcept
{
#if CD_OS_WINDOWS
    LARGE_INTEGER counter {};
    ::QueryPerformanceCounter(&counter);
    const std::int64_t freq = qpc_freq().hz();
    // counter * (1e9 / freq), computed carefully to avoid 64-bit overflow.
    // For typical freq (10 MHz on modern x64), counter fits in 47 bits per year.
    const auto seconds = static_cast<std::uint64_t>(counter.QuadPart / freq);
    const auto remainder = static_cast<std::uint64_t>(counter.QuadPart % freq);
    return seconds * 1'000'000'000ull + (remainder * 1'000'000'000ull) / static_cast<std::uint64_t>(freq);
#elif CD_OS_MACOS || CD_OS_IOS || CD_OS_TVOS || CD_OS_WATCHOS
    std::timespec ts {};
    ::clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#elif CD_OS_LINUX || CD_OS_ANDROID
    std::timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#else
    // POSIX fallback (BSD without RAW variants).
    std::timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

}  // namespace cd::time
