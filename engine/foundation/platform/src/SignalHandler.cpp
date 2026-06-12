// =============================================================================
// CHROMODYNAMIC — cd/platform/SignalHandler.cpp
// =============================================================================
#include <cd/platform/SignalHandler.hpp>

#include <atomic>
#include <csignal>

namespace cd::platform
{

namespace
{

// Process-wide active callback. Set under installed_flag ownership.
std::atomic<SignalCallback> g_callback { nullptr };
std::atomic<bool> g_installed { false };
std::atomic<bool> g_in_signal { false };  // re-entrancy guard for crash storm

extern "C" void cd_signal_dispatch(int sig) noexcept
{
    // Re-entrancy guard: if a second crash signal fires while we're already
    // dispatching the first, do not loop. Atomic exchange is async-signal-safe.
    bool expected = false;
    const bool first = g_in_signal.compare_exchange_strong(expected, true);
    if (!first)
    {
        return;
    }
    SignalCategory category = SignalCategory::kUnknown;
    switch (sig)
    {
        case SIGABRT:
        case SIGSEGV:
        case SIGFPE:
        case SIGILL:
            category = SignalCategory::kCrash;
            break;
        case SIGINT:
        case SIGTERM:
            category = SignalCategory::kInterrupt;
            break;
        default:
            category = SignalCategory::kUnknown;
            break;
    }
    SignalCallback cb = g_callback.load(std::memory_order_acquire);
    if (cb != nullptr)
    {
        cb(category, sig);
    }
    // Allow another signal to be observed after callback returns (interrupt
    // signals usually want this; crash signals typically terminated already).
    g_in_signal.store(false, std::memory_order_release);
}

}  // namespace

bool SignalHandler::install(SignalCallback cb) noexcept
{
    if (cb == nullptr)
    {
        return false;
    }
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;  // already installed elsewhere
    }
    g_callback.store(cb, std::memory_order_release);

    std::signal(SIGABRT, &cd_signal_dispatch);
    std::signal(SIGSEGV, &cd_signal_dispatch);
    std::signal(SIGFPE, &cd_signal_dispatch);
    std::signal(SIGILL, &cd_signal_dispatch);
    std::signal(SIGINT, &cd_signal_dispatch);
    std::signal(SIGTERM, &cd_signal_dispatch);

    installed_ = true;
    return true;
}

void SignalHandler::uninstall() noexcept
{
    if (!installed_)
    {
        return;
    }
    std::signal(SIGABRT, SIG_DFL);
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGFPE, SIG_DFL);
    std::signal(SIGILL, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);

    g_callback.store(nullptr, std::memory_order_release);
    g_installed.store(false, std::memory_order_release);
    installed_ = false;
}

bool SignalHandler::is_active() noexcept
{
    return g_installed.load(std::memory_order_acquire);
}

}  // namespace cd::platform
