// =============================================================================
// CHROMODYNAMIC — cd/diag/CrashReporter.hpp
// ADR-013 §C (Crash Reporting) + ADR-017 P0 (DfH crashreporter.hpp salvage)
//
// Minimal v1 — installs cd::platform::SignalHandler and dispatches into a
// user-provided reporter callback. The full Crashpad-backed pipeline (minidump
// generation, symbol upload, attachment-stream injection) is queued for
// Sprint S2.5 per ADR-013 §C roadmap. The v1 surface is forward-compatible:
// callers register a reporter, and the engine swap-in upgrades transparently.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/platform/SignalHandler.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::diag
{

enum class CrashSeverity : std::uint8_t
{
    kFatal,      // SIGABRT / SIGSEGV / SIGFPE / SIGILL — process terminating
    kNonFatal,   // soft-assert path; engine can continue
    kInterrupt,  // SIGINT / SIGTERM — graceful shutdown requested
};

struct CrashContext
{
    CrashSeverity severity { CrashSeverity::kFatal };
    int raw_signal { 0 };
    std::string_view label {};  // human-readable signal name (e.g., "SIGSEGV")
};

struct StackFrameSymbol
{
    std::uintptr_t address { 0 };
    std::string module_name {};
    std::string symbol_name {};
    std::uint32_t line { 0 };
};

/// User-provided reporter. MUST be async-signal-safe when severity == Fatal.
/// For NonFatal/Interrupt the function is called on the originating thread
/// and may use the full standard library.
using ReporterFn = void (*)(const CrashContext& ctx) noexcept;

class CrashReporter
{
public:
    CrashReporter() noexcept = default;

    ~CrashReporter()
    {
        uninstall();
    }

    CrashReporter(const CrashReporter&) = delete;
    CrashReporter& operator=(const CrashReporter&) = delete;

    /// Install signal handler that routes Fatal/Interrupt categories into
    /// `reporter`. Returns false if a handler is already active.
    [[nodiscard]] bool install(ReporterFn reporter) noexcept;

    void uninstall() noexcept;

    /// Set the directory where minidumps will be written (Sprint S2.5+).
    /// Stored as plain string; not used in v1 dispatch path.
    void set_dump_directory(std::string dir)
    {
        dump_directory_ = std::move(dir);
    }

    [[nodiscard]] std::string_view dump_directory() const noexcept
    {
        return dump_directory_;
    }

    /// Synchronously report a non-fatal incident. Useful for soft-asserts and
    /// recoverable rendering failures. Does not terminate the process.
    void capture_non_fatal(std::string_view label) noexcept;

    /// FAZ 10 Wave 10.2 — Capture symbolicated stack backtrace frames.
    [[nodiscard]] static std::vector<StackFrameSymbol>
    capture_stack_backtrace(std::size_t max_frames = 32);

    /// FAZ 10 Wave 10.2 — Write a structured .cdcrash minidump & symbolicated
    /// backtrace report into dump_directory() (or return the formatted report
    /// payload if dump_directory() is empty).
    [[nodiscard]] std::string write_minidump_report(
        std::string_view reason,
        CrashSeverity severity = CrashSeverity::kFatal
    ) const;

    [[nodiscard]] bool is_installed() const noexcept
    {
        return installed_;
    }

private:
    cd::platform::SignalHandler signal_handler_ {};
    std::string dump_directory_ {};
    bool installed_ { false };
};

}  // namespace cd::diag

