// =============================================================================
// CHROMODYNAMIC — cd/profile/Scope.hpp
//
// Minimal CPU scope-timer + pluggable sink. NOT a tracing framework — that
// belongs to a future cd::tracy / cd::tracing integration. This is the
// low-overhead, zero-dependency layer engineering code uses to bracket
// regions of interest and feed the results to:
//   * stdout (default sink) for quick local timing
//   * a CSV file for offline analysis
//   * the future ImGui HUD overlay (W10.3) for live frame stats
//
// Design:
//   * `CD_PROFILE_SCOPE(name)` opens a scope timer; the closing brace
//     records the duration via the registered sink.
//   * Sinks are global, swappable, and thread-safe at registration time
//     (atomic pointer swap). Recording is lock-free per sink — the
//     default in-memory sink uses a per-thread ring buffer.
//   * Zero-overhead when CD_ENABLE_PROFILE=OFF (the macro expands to a
//     comment). On by default in Debug + RelWithDebInfo, OFF for shipping
//     Release builds.
//
// Not in v1:
//   * GPU scopes — see W15.2 (RDOC/RGP labels) for the GPU side.
//   * Hierarchical / call-tree views — sink is responsible for that.
//   * Sampling profiler — this is INSTRUMENTATION only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace cd::profile
{

/// One recorded sample. Sinks consume these in whatever shape they like
/// (CSV append, ring buffer, JSON emit, ImGui plot). Designed to be
/// trivially copyable so producers can pass them by value without a
/// thought about lifetime.
struct Sample
{
    std::string_view name;            ///< Compile-time literal — sink may NOT free it.
    std::uint64_t start_ns { 0 };     ///< steady_clock-based, monotonic.
    std::uint64_t duration_ns { 0 };
    std::uint64_t thread_hash { 0 };  ///< Cheap thread id hash so multi-thread views can group.
};

/// Sink interface — concrete sinks live in their own TUs (StdoutSink,
/// CsvSink, BufferSink) and register via set_sink().
class ISink
{
public:
    ISink() noexcept = default;
    virtual ~ISink() = default;
    ISink(const ISink&) = delete;
    ISink& operator=(const ISink&) = delete;
    ISink(ISink&&) = delete;
    ISink& operator=(ISink&&) = delete;

    virtual void submit(const Sample& s) noexcept = 0;
};

/// Returns the currently installed sink. Never null after process init —
/// a no-op `NullSink` is installed at startup.
[[nodiscard]] ISink* current_sink() noexcept;

/// Swap the active sink. The previous sink is returned so the caller can
/// chain or restore on RAII scope exit. Thread-safe (acquire/release).
ISink* set_sink(ISink* next) noexcept;

namespace detail
{

[[nodiscard]] std::uint64_t now_ns() noexcept;
[[nodiscard]] std::uint64_t current_thread_hash() noexcept;

}  // namespace detail

/// RAII scope timer. Construct at the top of a scope, the destructor
/// records the elapsed time to the active sink. The `name` parameter
/// MUST point at storage that outlives the sample's consumption — using
/// a string literal at the call site (which CD_PROFILE_SCOPE enforces)
/// is the right pattern.
class Scope
{
public:
    explicit Scope(std::string_view name) noexcept
        : name_ { name }
        , start_ns_ { detail::now_ns() }
    {
    }

    ~Scope()
    {
        if (auto* sink = current_sink(); sink != nullptr)
        {
            const auto end_ns = detail::now_ns();
            Sample s {};
            s.name = name_;
            s.start_ns = start_ns_;
            s.duration_ns = end_ns - start_ns_;
            s.thread_hash = detail::current_thread_hash();
            sink->submit(s);
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

private:
    std::string_view name_;
    std::uint64_t start_ns_;
};

}  // namespace cd::profile

// ---- Macros ---------------------------------------------------------------
//
// Use a unique variable name per call site so multiple scopes inside the
// same block (different `if` arms etc.) do not collide. CD_PROFILE_CAT
// avoids the standard `##` token-pasting pitfall when one of the operands
// is itself a macro.

#define CD_PROFILE_CAT_(a, b) a##b
#define CD_PROFILE_CAT(a, b)  CD_PROFILE_CAT_(a, b)

#if !defined(CD_ENABLE_PROFILE)
    #define CD_ENABLE_PROFILE 1
#endif

#if CD_ENABLE_PROFILE
    /// Drop this at the top of any scope to time it. `name` should be a string
    /// literal — sinks store the pointer, not the contents.
    #define CD_PROFILE_SCOPE(name)                                     \
        ::cd::profile::Scope CD_PROFILE_CAT(_cd_prof_scope_, __LINE__) \
        {                                                              \
            name                                                       \
        }
#else
    #define CD_PROFILE_SCOPE(name) static_cast<void>(0)
#endif
