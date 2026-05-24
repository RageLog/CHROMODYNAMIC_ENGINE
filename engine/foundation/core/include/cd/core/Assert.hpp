// =============================================================================
// CHROMODYNAMIC — cd/core/Assert.hpp
// Phase 96.A / Wave 264 — debug/release assertion macros.
//
// Two macros:
//
//   CD_ASSERT(cond)            — debug-only invariant; expanded to no-op
//                                in release. Use for "I claim this can
//                                never happen by construction".
//   CD_VERIFY(cond)            — always-on; aborts on false even in
//                                release. Use for "this comes from
//                                external input that I'm validating".
//
// On failure both call `cd::core::assert_handler(file, line, expr)`
// which by default writes to stderr and calls `std::abort()`. The
// handler can be redirected via `set_assert_handler()` for in-engine
// crash collection / dev-only "ignore, continue" UI.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdio>
#include <cstdlib>

namespace cd::core
{

using AssertHandler = void(*)(const char* file, int line, const char* expr);

namespace detail
{
inline AssertHandler& assert_handler_slot() noexcept
{
    static AssertHandler h = nullptr;
    return h;
}
}  // namespace detail

inline void set_assert_handler(AssertHandler h) noexcept
{
    detail::assert_handler_slot() = h;
}

[[noreturn]] inline void default_assert_fail(const char* file, int line,
                                             const char* expr) noexcept
{
    std::fprintf(stderr, "CD_ASSERT(%s) failed at %s:%d\n", expr, file, line);
    std::abort();
}

inline void invoke_assert(const char* file, int line, const char* expr) noexcept
{
    auto h = detail::assert_handler_slot();
    if (h) h(file, line, expr);
    default_assert_fail(file, line, expr);
}

}  // namespace cd::core

#if defined(NDEBUG)
    #define CD_ASSERT(cond) ((void)0)
#else
    #define CD_ASSERT(cond)                                              \
        do {                                                             \
            if (!(cond)) ::cd::core::invoke_assert(__FILE__, __LINE__, #cond); \
        } while (0)
#endif

#define CD_VERIFY(cond)                                                  \
    do {                                                                 \
        if (!(cond)) ::cd::core::invoke_assert(__FILE__, __LINE__, #cond); \
    } while (0)
