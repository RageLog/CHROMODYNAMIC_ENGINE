// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Atomics.hpp
// ADR-005 §G + ADR-015 (Concurrency, Job System & SIMD)
//
// Atomic helpers + memory-order semantic aliases. The engine prefers explicit
// acquire/release over seq_cst on hot paths; this header gives those uses
// readable names so the audit (`safety-integration` agent) can lint quickly.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>

namespace cd::concurrency
{

// Memory-order semantic aliases — pick by *intent*, not by raw enum value.
//   - `acquire_load`     : observed value publishes everything written before.
//   - `release_store`    : everything written before is observed by acquire-load.
//   - `acq_rel`          : RMW that combines both.
//   - `relaxed`          : counter-style; no synchronisation, only atomicity.
//   - `seq_cst`          : totally ordered globally. Use sparingly.
inline constexpr auto acquire_order = std::memory_order_acquire;
inline constexpr auto release_order = std::memory_order_release;
inline constexpr auto acq_rel_order = std::memory_order_acq_rel;
inline constexpr auto relaxed_order = std::memory_order_relaxed;
inline constexpr auto seq_cst_order = std::memory_order_seq_cst;

/// `cpu_pause()` — give the CPU a hint that we're spinning, so it can pace the
/// pipeline and lower power. On x86_64 emits `PAUSE`; on ARM64 emits `YIELD`.
CD_FORCE_INLINE void cpu_pause() noexcept
{
#if CD_ARCH_X86_64 || CD_ARCH_X86
    #if defined(_MSC_VER) && !defined(__clang__)
    _mm_pause();
    #else
    __builtin_ia32_pause();
    #endif
#elif CD_ARCH_ARM64 || CD_ARCH_ARM
    #if defined(_MSC_VER) && !defined(__clang__)
    __yield();
    #else
    __asm__ __volatile__("yield" ::: "memory");
    #endif
#else
    // Generic fallback: prevent the compiler from coalescing reads in a spin.
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

}  // namespace cd::concurrency
