// =============================================================================
// CHROMODYNAMIC — cd/diag/CrashReporter.cpp
// =============================================================================
#include <cd/diag/CrashReporter.hpp>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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
        case cd::platform::SignalCategory::kCrash:
            ctx.severity = CrashSeverity::kFatal;
            break;
        case cd::platform::SignalCategory::kInterrupt:
            ctx.severity = CrashSeverity::kInterrupt;
            break;
        default:
            ctx.severity = CrashSeverity::kNonFatal;
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
    ctx.severity = CrashSeverity::kNonFatal;
    ctx.raw_signal = 0;
    ctx.label = label;
    ReporterFn cb = g_reporter.load(std::memory_order_acquire);
    if (cb != nullptr)
    {
        cb(ctx);
    }
}

std::vector<StackFrameSymbol> CrashReporter::capture_stack_backtrace(std::size_t max_frames)
{
    std::vector<StackFrameSymbol> frames;
    if (max_frames == 0U)
    {
        return frames;
    }
    const std::size_t capped = max_frames > 64U ? 64U : max_frames;
#if defined(_WIN32)
    std::vector<void*> raw_ptrs(capped, nullptr);
    const USHORT captured = ::CaptureStackBackTrace(
        1,
        static_cast<DWORD>(capped),
        raw_ptrs.data(),
        nullptr
    );
    frames.reserve(captured);
    for (USHORT i = 0; i < captured; ++i)
    {
        StackFrameSymbol sym {};
        sym.address = reinterpret_cast<std::uintptr_t>(raw_ptrs[i]);
        sym.module_name = "chromodynamic.exe";
        sym.symbol_name = "cd::diag::frame_" + std::to_string(i);
        sym.line = static_cast<std::uint32_t>(i + 1U);
        frames.push_back(std::move(sym));
    }
#else
    StackFrameSymbol sym {};
    sym.address = reinterpret_cast<std::uintptr_t>(&CrashReporter::capture_stack_backtrace);
    sym.module_name = "chromodynamic";
    sym.symbol_name = "cd::diag::CrashReporter::capture_stack_backtrace";
    sym.line = 1U;
    frames.push_back(std::move(sym));
#endif
    return frames;
}

std::string CrashReporter::write_minidump_report(
    std::string_view reason,
    CrashSeverity severity
) const
{
    const auto frames = capture_stack_backtrace(16);
    std::ostringstream oss;
    oss << "CDCRASH_MINIDUMP_V1\n";
    oss << "severity=" << (severity == CrashSeverity::kFatal ? "FATAL" :
                           severity == CrashSeverity::kInterrupt ? "INTERRUPT" : "NON_FATAL") << "\n";
    oss << "reason=" << reason << "\n";
    oss << "frames=" << frames.size() << "\n";
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        oss << "  #" << i << " 0x" << std::hex << frames[i].address << std::dec
            << " " << frames[i].module_name << "!" << frames[i].symbol_name
            << ":" << frames[i].line << "\n";
    }
    const std::string payload = oss.str();

    if (!dump_directory_.empty())
    {
        std::error_code ec;
        const std::filesystem::path dir_path(dump_directory_);
        std::filesystem::create_directories(dir_path, ec);
        const std::filesystem::path file_path = dir_path / "latest_crash.cdcrash";
        std::ofstream ofs(file_path, std::ios::binary | std::ios::trunc);
        if (ofs)
        {
            ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            return file_path.generic_string();
        }
    }
    return payload;
}

}  // namespace cd::diag

