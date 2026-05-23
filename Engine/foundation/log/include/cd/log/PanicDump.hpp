// =============================================================================
// CHROMODYNAMIC — cd/log/PanicDump.hpp
// Phase 10 / Sprint 1 / Wave 111 — diag → log bridge.
//
// Wires a cd::log::RingBufferSink into cd::diag's panic path so that
// when an invariant breaks, the last N log records mirror to stderr
// *before* the structured PanicInfo. That makes crash triage practical:
// a file sink that hadn't flushed is no longer your only chance to see
// what the engine was doing right before death.
//
// Direction: cd::log depends on cd::diag (never the reverse), so the
// bridge lives here. The panic handler is a free function — it can't
// capture state, so the sink pointer lives in an `inline std::atomic`
// at TU scope, set by `install_panic_dump_handler`.
//
// Header-only because cd::log is an INTERFACE library.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/log/RingBufferSink.hpp>

#include <atomic>
#include <cstdio>

namespace cd::log
{

namespace detail
{

inline std::atomic<RingBufferSink*> g_panic_dump_sink { nullptr };

inline void panic_dump_handler(const cd::diag::PanicInfo& info) noexcept
{
    auto* sink = g_panic_dump_sink.load(std::memory_order_acquire);
    if (sink != nullptr)
    {
        const auto snap = sink->snapshot();
        std::fprintf(stderr,
                     "\n--- log mirror (last %zu records%s) ---\n",
                     snap.size(),
                     sink->wrapped() ? ", buffer wrapped" : "");
        for (const auto& r : snap)
        {
            std::fprintf(stderr, "  %s\n", r.message.c_str());
        }
    }

    std::fprintf(stderr, "\n=== CHROMODYNAMIC PANIC ===\n");
    std::fprintf(stderr, "  file : %.*s:%u\n",
                 static_cast<int>(info.file.size()), info.file.data(), info.line);
    if (!info.function.empty())
        std::fprintf(stderr, "  fn   : %.*s\n",
                     static_cast<int>(info.function.size()), info.function.data());
    if (!info.expression.empty())
        std::fprintf(stderr, "  expr : %.*s\n",
                     static_cast<int>(info.expression.size()), info.expression.data());
    if (!info.message.empty())
        std::fprintf(stderr, "  msg  : %.*s\n",
                     static_cast<int>(info.message.size()), info.message.data());
    std::fprintf(stderr, "===========================\n");
    std::fflush(stderr);
}

}  // namespace detail

/// Install the dump handler. `sink` must outlive every potential panic
/// call (typical lifetime: program duration). Pass nullptr to keep the
/// dump prefix off but still route panics through this handler. Returns
/// the previous panic handler so the caller can restore it later.
inline cd::diag::PanicHandler install_panic_dump_handler(RingBufferSink* sink) noexcept
{
    detail::g_panic_dump_sink.store(sink, std::memory_order_release);
    return cd::diag::set_panic_handler(&detail::panic_dump_handler);
}

/// Read-only accessor for tests.
[[nodiscard]] inline RingBufferSink* current_panic_dump_sink() noexcept
{
    return detail::g_panic_dump_sink.load(std::memory_order_acquire);
}

}  // namespace cd::log
