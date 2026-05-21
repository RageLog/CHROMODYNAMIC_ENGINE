// =============================================================================
// CHROMODYNAMIC — cd/diag/CrashReporter.cpp
// =============================================================================
#include <cd/diag/CrashReporter.hpp>

#include <atomic>
#include <csignal>

namespace cd::diag
{

namespace
{

// Process-wide reporter pointer set by the *active* CrashReporter instance.
// The signal-time dispatcher reads this with acquire ordering.
std::atomic<ReporterFn> g_reporter { nullptr };

constexpr std::string_view label_for(int sig) noexcept
{
    switch (sig)
    {
        case SIGABRT:
            return "SIGABRT";
        case SIGSEGV:
            return "SIGSEGV";
        case SIGFPE:
            return "SIGFPE";
        case SIGILL:
            return "SIGILL";
        case SIGINT:
            return "SIGINT";
        case SIGTERM:
            return "SIGTERM";
        default:
            return "UNKNOWN";
    }
}

void dispatch_signal(cd::platform::SignalCategory category, int raw) noexcept
{
    CrashContext ctx {};
    ctx.raw_signal = raw;
    ctx.label = label_for(raw);
    switch (category)
    {
        case cd::platform::SignalCategory::Crash:
            ctx.severity = CrashSeverity::Fatal;
            break;
        case cd::platform::SignalCategory::Interrupt:
            ctx.severity = CrashSeverity::Interrupt;
            break;
        default:
            ctx.severity = CrashSeverity::NonFatal;
            break;
    }
    ReporterFn cb = g_reporter.load(std::memory_order_acquire);
    if (cb != nullptr)
    {
        cb(ctx);
    }
}

}  // namespace

bool CrashReporter::install(ReporterFn reporter) noexcept
{
    if (reporter == nullptr || installed_)
    {
        return false;
    }
    g_reporter.store(reporter, std::memory_order_release);
    if (!signal_handler_.install(&dispatch_signal))
    {
        g_reporter.store(nullptr, std::memory_order_release);
        return false;
    }
    installed_ = true;
    return true;
}

void CrashReporter::uninstall() noexcept
{
    if (!installed_)
    {
        return;
    }
    signal_handler_.uninstall();
    g_reporter.store(nullptr, std::memory_order_release);
    installed_ = false;
}

void CrashReporter::capture_non_fatal(std::string_view label) noexcept
{
    CrashContext ctx {};
    ctx.severity = CrashSeverity::NonFatal;
    ctx.raw_signal = 0;
    ctx.label = label;
    ReporterFn cb = g_reporter.load(std::memory_order_acquire);
    if (cb != nullptr)
    {
        cb(ctx);
    }
}

}  // namespace cd::diag
