// =============================================================================
// CHROMODYNAMIC — cd/diag/Assert.hpp
// Phase 10 / Sprint 1 / Wave 107 — assertion macros + structured error stack.
//
// Three flavours of run-time check, mapped to the standard engine
// configuration model:
//
//   CD_ASSERT(expr)          — debug-only sanity check. No-op in
//                              release. On failure: invokes the
//                              installed panic handler with file/line/
//                              expression captured in a structured
//                              cd::diag::PanicInfo.
//   CD_VERIFY(expr)          — release-active check (kept in every
//                              build). Still calls the panic handler
//                              on failure. Use for invariants that
//                              must hold even in shipping builds.
//   CD_PANIC(message)         — unconditional panic with a message.
//                              Useful for "unreachable" branches.
//
// Panic handler:
//   * Default: writes the panic info to stderr (file:line: expr;
//     optional message; optional backtrace) and aborts.
//   * Replace via cd::diag::set_panic_handler(custom) for unit tests
//     ("throw instead of abort"), telemetry sinks, crash reporters,
//     etc. The handler returns void; if it returns, the engine still
//     aborts so a misbehaving custom handler can't hide an invariant
//     break.
//
// Header-only; cd::core for ErrorCode + Result types only. The free
// function `set_panic_handler` lives in cd::diag's library TU so the
// handler pointer has process-wide storage.
//
// Why a new assertion layer instead of <cassert>:
//   * Stable structured info — file/line/expr/msg in a typed struct
//     callers can route to telemetry, not just printf.
//   * Override hook — unit tests need "convert panic to exception",
//     production wants telemetry; same surface, different sink.
//   * No global state visible to client code — the handler ptr lives
//     in cd::diag, not in client TU.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <source_location>
#include <string_view>

/// @addtogroup cd_foundation_diag
/// @{
namespace cd::diag
{

/// Structured payload the panic handler receives. file/line come
/// from std::source_location at the macro callsite; message is the
/// caller's std::string_view (may be empty).
struct PanicInfo
{
    std::string_view expression {};        ///< CD_ASSERT(expr) → "expr"; CD_PANIC → empty.
    std::string_view message {};           ///< Optional human-readable context.
    std::string_view file {};
    std::uint32_t line { 0 };
    std::string_view function {};
};

/// Process-wide hook signature. The handler MAY return (in which case
/// the engine still calls std::abort right after) or terminate via
/// std::terminate / longjmp / exception (unit tests).
using PanicHandler = void (*)(const PanicInfo&);

/// Replace the current handler. Returns the previous one for chaining.
[[nodiscard]] PanicHandler set_panic_handler(PanicHandler handler) noexcept;

/// Read the current handler (mostly for tests).
[[nodiscard]] PanicHandler current_panic_handler() noexcept;

/// Reset to the default handler (stderr + abort). Returns the prior
/// handler in case the caller wants to restore later.
PanicHandler reset_panic_handler() noexcept;

/// Internal: invoke the handler then abort. Always [[noreturn]] —
/// the handler may throw / longjmp / std::terminate, or return
/// normally (in which case we call std::abort()). NOT `noexcept`:
/// a throwing handler (e.g. unit-test "convert panic to exception")
/// must be allowed to propagate; declaring noexcept would force
/// std::terminate the moment the handler throws.
[[noreturn]] void panic(const PanicInfo& info);

}  // namespace cd::diag
/// @}

// -- Macros -------------------------------------------------------------------
//
// Wrapped in do-while(0) so they parse as a statement everywhere
// (`if (x) CD_ASSERT(y); else ...` works without surprises).

#define CD_PANIC_RAW(EXPR_STR, MSG)                                          \
    do {                                                                     \
        const auto _cd_diag_loc = std::source_location::current();           \
        ::cd::diag::PanicInfo _cd_pi;                                        \
        _cd_pi.expression = (EXPR_STR);                                      \
        _cd_pi.message    = (MSG);                                           \
        _cd_pi.file       = _cd_diag_loc.file_name();                        \
        _cd_pi.line       = _cd_diag_loc.line();                             \
        _cd_pi.function   = _cd_diag_loc.function_name();                    \
        ::cd::diag::panic(_cd_pi);                                           \
    } while (false)

/// Always-on invariant. Stays in release builds.
#define CD_VERIFY(EXPR)                                                      \
    do {                                                                     \
        if (!(EXPR)) {                                                       \
            CD_PANIC_RAW(#EXPR, "verify failed");                            \
        }                                                                    \
    } while (false)

/// Always-on invariant with custom message.
#define CD_VERIFY_MSG(EXPR, MSG)                                             \
    do {                                                                     \
        if (!(EXPR)) {                                                       \
            CD_PANIC_RAW(#EXPR, (MSG));                                      \
        }                                                                    \
    } while (false)

/// Unconditional panic.
#define CD_PANIC(MSG) CD_PANIC_RAW("", (MSG))

#if defined(NDEBUG)
    #define CD_ASSERT(EXPR)            ((void)0)
    #define CD_ASSERT_MSG(EXPR, MSG)   ((void)0)
#else
    /// Debug-only sanity check. Stripped in release.
    #define CD_ASSERT(EXPR)            CD_VERIFY(EXPR)
    #define CD_ASSERT_MSG(EXPR, MSG)   CD_VERIFY_MSG(EXPR, MSG)
#endif
