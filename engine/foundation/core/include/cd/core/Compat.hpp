// =============================================================================
// CHROMODYNAMIC — cd/core/Compat.hpp
// ADR-017 P0 — DtForHil compat.hpp salvage
//
// Cross-platform compatibility helpers that paper over differences between
// MSVC / Clang / GCC and Windows / POSIX. Free functions; no global state.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <memory>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace cd::core::compat
{

/// Null-terminated copy into a fixed-size destination buffer with bounds
/// safety. Always null-terminates if `destination_size > 0`.
inline void copy_c_string(char* destination, std::size_t destination_size, std::string_view source) noexcept
{
    if (destination == nullptr || destination_size == 0)
    {
        return;
    }
    const std::size_t copy_length = (std::min)(destination_size - 1, source.size());
    if (copy_length > 0)
    {
        std::memcpy(destination, source.data(), copy_length);
    }
    destination[copy_length] = '\0';
}

inline void copy_c_string(char* destination, std::size_t destination_size, const char* source) noexcept
{
    copy_c_string(destination, destination_size, source != nullptr ? std::string_view { source } : std::string_view {});
}

/// std::popcount alias returning int for legacy interop. C++23 std::popcount
/// is constexpr; this wrapper is consteval-friendly when the input is constant.
[[nodiscard]] inline int popcount64(std::uint64_t value) noexcept
{
    // std::popcount already returns int; no cast needed (GCC -Wuseless-cast).
    return std::popcount(value);
}

/// Safe environment-variable read across MSVC (_dupenv_s) and POSIX (getenv).
/// Returns empty string when the variable is unset or `name` is invalid.
[[nodiscard]] inline std::string read_env_var(const char* name)
{
    if (name == nullptr || *name == '\0')
    {
        return {};
    }
#if CD_PLATFORM_WINDOWS
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr)
    {
        return {};
    }
    const std::unique_ptr<char, decltype(&std::free)> owned {
        buffer, &std::free };  // RAII for the _dupenv_s allocation
    std::string value { owned.get() };
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string { value } : std::string {};
#endif
}

/// Thread-safe wrapper over std::localtime. Uses localtime_s on MSVC and
/// localtime_r on POSIX; returns a zeroed std::tm on failure.
[[nodiscard]] inline std::tm local_time(std::time_t value) noexcept
{
    std::tm result {};
#if CD_OS_WINDOWS
    ::localtime_s(&result, &value);
#elif CD_OS_POSIX
    ::localtime_r(&value, &result);
#else
    #error "cd::core::compat::local_time: no thread-safe localtime on this platform"
#endif
    return result;
}

}  // namespace cd::core::compat
