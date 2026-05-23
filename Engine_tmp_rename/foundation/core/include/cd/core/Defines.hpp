// =============================================================================
// CHROMODYNAMIC — cd/core/Defines.hpp
// Phase 2 Sprint S2.1 — ADR-017 P0 (DtForHil dfh_definitions.hpp salvage)
//
// This header is the **engine-side entry point** for low-level compiler /
// platform / arch / SIMD detection. It delegates the heavy lifting to
// cd/core/Definitions.hpp (2200+ line DfH-derived header) and layers on top:
//
//   1. Legacy alias namespace (CD_PLATFORM_* → CD_OS_*) used by Sprint S2.0
//      consumers; preserved so we never break a 1:1 port.
//   2. <version>-driven C++26 opt-in feature gates (CD_HAS_CXX26_*) — these
//      live outside DfH definitions because they describe library availability
//      rather than compiler identity.
//   3. Per-library API export macro CD_CORE_API.
//   4. kCacheLineSize constant + helpers (ignore_unused, CD_IGNORE_UNUSED).
//
// All library code should include <cd/core/Defines.hpp>; advanced consumers
// may include <cd/core/Definitions.hpp> directly for SIMD / OS aggregate /
// version-comparison helpers.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// 1. DfH-salvaged definitions (compiler / OS / arch / SIMD / attributes / casts)
// -----------------------------------------------------------------------------
#include <cd/core/Definitions.hpp>

// -----------------------------------------------------------------------------
// 2. C++23 minimum + <version> for SD-6 feature-test macros
// -----------------------------------------------------------------------------
#if !defined(__cplusplus) || (__cplusplus < 202302L && !defined(_MSC_VER))
    #error "CHROMODYNAMIC requires C++23 (or newer); see ADR-005"
#endif

#include <version>

// -----------------------------------------------------------------------------
// 3. Legacy CD_PLATFORM_* aliases over CD_OS_* (Sprint S2.0 compat)
//    Definitions.hpp uses CD_OS_WINDOWS/MACOS/LINUX/etc.; engine code so far
//    references CD_PLATFORM_*. Keep both surfaces; prefer CD_OS_* in new code.
// -----------------------------------------------------------------------------
#if CD_OS_WINDOWS
    #define CD_PLATFORM_WINDOWS 1
#endif
#if CD_OS_LINUX && !CD_OS_ANDROID
    #define CD_PLATFORM_LINUX 1
#endif
#if CD_OS_ANDROID
    #define CD_PLATFORM_ANDROID 1
#endif
#if CD_OS_MACOS
    #define CD_PLATFORM_MACOS 1
#endif
#if CD_OS_IOS
    #define CD_PLATFORM_IOS 1
#endif
#if CD_OS_APPLE
    #define CD_PLATFORM_APPLE 1
#endif
#if CD_OS_POSIX
    #define CD_PLATFORM_POSIX 1
#endif
#if CD_OS_EMSCRIPTEN
    #define CD_PLATFORM_WEB 1
#endif

// CD_COMPILER_* are already defined as 1/0 in Definitions.hpp; legacy code may
// `#if defined(CD_COMPILER_CLANG)` form. Convert presence-style for that form.
#if CD_COMPILER_CLANG_CL && !defined(CD_COMPILER_CLANG_CL_DEFINED)
    #define CD_COMPILER_CLANG_CL_DEFINED 1
#endif

// CD_ARCH_* aliases: Definitions.hpp uses CD_ARCH_X86_64/ARM64/RISCV64. Keep
// same names. Provide a single defined() probe for sanity.
#if !(CD_ARCH_X86_64 || CD_ARCH_ARM64 || CD_ARCH_RISCV64 || CD_ARCH_X86 || CD_ARCH_ARM)
    #error "CHROMODYNAMIC: no supported architecture detected"
#endif

// -----------------------------------------------------------------------------
// 4. C++26 opt-in feature gates (ADR-016 §C)
// -----------------------------------------------------------------------------
#if defined(__cpp_lib_reflection) && __cpp_lib_reflection >= 202600L
    #define CD_HAS_CXX26_REFLECTION 1
#else
    #define CD_HAS_CXX26_REFLECTION 0
#endif

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202300L
    #define CD_HAS_CXX26_EXECUTION 1
#else
    #define CD_HAS_CXX26_EXECUTION 0
#endif

#if defined(__cpp_contracts) && __cpp_contracts >= 202600L
    #define CD_HAS_CXX26_CONTRACTS 1
#else
    #define CD_HAS_CXX26_CONTRACTS 0
#endif

#if defined(__cpp_pattern_matching) && __cpp_pattern_matching >= 202600L
    #define CD_HAS_CXX26_PATTERN_MATCHING 1
#else
    #define CD_HAS_CXX26_PATTERN_MATCHING 0
#endif

// 202211L = P2505R5 monadic operations (and_then / or_else / transform[_error]).
// cd::core::Result uses the monadic API throughout; the lower 202202L
// threshold (base <expected> only) would compile but link callers that need
// the monadic ops. Minimum stdlib versions that ship them:
//   libstdc++ 13 (GCC 13+) - libc++ 17 (Clang 17+) - MSVC STL 19.36 (VS 17.6+).
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
    #define CD_HAS_STD_EXPECTED 1
#else
    #define CD_HAS_STD_EXPECTED 0
#endif

// -----------------------------------------------------------------------------
// 5. Per-library API export — CD_CORE_API (and pattern for other libs)
//    Definitions.hpp ships CD_HELPER_DLL_EXPORT/IMPORT/LOCAL (DfH-style names).
//    Provide a thin CHROMODYNAMIC alias and the cd_core flavour.
// -----------------------------------------------------------------------------
#define CD_DLL_EXPORT CD_HELPER_DLL_EXPORT
#define CD_DLL_IMPORT CD_HELPER_DLL_IMPORT
#define CD_DLL_LOCAL  CD_HELPER_DLL_LOCAL

#if defined(CD_CORE_BUILD_DLL)
    #define CD_CORE_API CD_DLL_EXPORT
#elif defined(CD_CORE_DLL)
    #define CD_CORE_API CD_DLL_IMPORT
#else
    #define CD_CORE_API
#endif

// -----------------------------------------------------------------------------
// 6. Lifecycle attribute aliases (some are defined in Definitions.hpp; keep
//    these as canonical engine names for hot-path code)
// -----------------------------------------------------------------------------
#ifndef CD_NODISCARD
    #define CD_NODISCARD [[nodiscard]]
#endif
#ifndef CD_MAYBE_UNUSED
    #define CD_MAYBE_UNUSED [[maybe_unused]]
#endif
#ifndef CD_DEPRECATED
    #define CD_DEPRECATED(msg) [[deprecated(msg)]]
#endif
#ifndef CD_FALLTHROUGH
    #if defined(__has_cpp_attribute) && __has_cpp_attribute(fallthrough)
        #define CD_FALLTHROUGH [[fallthrough]]
    #else
        #define CD_FALLTHROUGH
    #endif
#endif

// -----------------------------------------------------------------------------
// 7. Cache-line + alignment helpers
// -----------------------------------------------------------------------------
// Use a fixed constant rather than std::hardware_destructive_interference_size:
//   1. The std value is allowed to vary with -mtune / -mcpu on GCC, which makes
//      it unsafe in any header that crosses ABI boundaries (GCC raises
//      -Winterference-size to enforce exactly this).
//   2. 64 bytes is the cache-line size of every x86-64 (Intel/AMD) and ARM64
//      (modern Apple/Qualcomm/Ampere) target we care about. Power9 uses 128;
//      we will conditionally override on that target when it ships.
#include <cstddef>

namespace cd::core
{
inline constexpr std::size_t kCacheLineSize = 64;
}  // namespace cd::core

#ifndef CD_ALIGN
    #define CD_ALIGN(N) alignas(N)
#endif
#define CD_CACHE_ALIGN alignas(::cd::core::kCacheLineSize)

// -----------------------------------------------------------------------------
// 8. Common typedefs + ignore-unused helper
// -----------------------------------------------------------------------------
namespace cd::core
{

using byte = unsigned char;

template <class T>
constexpr void ignore_unused(T&&) noexcept
{
}

}  // namespace cd::core

#define CD_IGNORE_UNUSED(x) ::cd::core::ignore_unused(x)
