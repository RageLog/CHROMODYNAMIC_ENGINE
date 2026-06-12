// =============================================================================
// CHROMODYNAMIC — cd/platform/SignalHandler.hpp
// ADR-005 §G + ADR-017 P0 (DfH common/utility/signalmanager.hpp salvage)
//
// POSIX <csignal> + Windows SEH bridge for crash & interrupt signals. Process-
// wide handlers (only one can be installed at a time); use RAII Scope to
// auto-uninstall on teardown.
//
// Notes:
//   - Signal handlers run under severe constraints — only async-signal-safe
//     functions may be called. The callback type is intentionally minimal.
//   - On Windows, <csignal>'s SIGSEGV/SIGFPE catch behaviour is incomplete;
//     a true crash trap requires SetUnhandledExceptionFilter (Sprint S2.1.f
//     extension via cd::diag::CrashReporter on top of this layer).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <csignal>
#include <cstdint>

namespace cd::platform
{

/// Categories of signals tracked by the handler.
enum class SignalCategory : std::uint8_t
{
    kCrash,      // SIGABRT / SIGSEGV / SIGFPE / SIGILL
    kInterrupt,  // SIGINT / SIGTERM (graceful shutdown request)
    kUnknown,
};

/// Function-pointer callback type. Must be async-signal-safe.
/// `raw_signal` is the platform signal number (SIGSEGV, SIGTERM, ...).
using SignalCallback = void (*)(SignalCategory category, int raw_signal) noexcept;

/// Install / uninstall process-wide signal handlers.
///
/// Thread-safe install/uninstall guarded by an atomic flag — only one set of
/// handlers can be live at a time. Re-installing returns false.
class SignalHandler
{
public:
    SignalHandler() noexcept = default;

    ~SignalHandler()
    {
        uninstall();
    }

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;
    SignalHandler(SignalHandler&&) = delete;
    SignalHandler& operator=(SignalHandler&&) = delete;

    /// Install `cb` as the active handler for both crash and interrupt
    /// categories. Returns false if a handler is already installed.
    [[nodiscard]] bool install(SignalCallback cb) noexcept;

    /// Restore default signal handlers. No-op if not installed.
    void uninstall() noexcept;

    [[nodiscard]] bool is_installed() const noexcept
    {
        return installed_;
    }

    /// Free-function querying whether *any* SignalHandler is currently active
    /// process-wide.
    [[nodiscard]] static bool is_active() noexcept;

private:
    bool installed_ { false };
};

}  // namespace cd::platform
