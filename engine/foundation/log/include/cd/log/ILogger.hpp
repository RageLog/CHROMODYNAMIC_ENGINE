// =============================================================================
// CHROMODYNAMIC — cd/log/ILogger.hpp
// ADR-013 + ADR-017 P1 (DfH logging/ilogger.hpp salvage, distilled)
//
// Engine-wide logging interface. Plugin/IPlugin coupling removed; ILogger is
// pure virtual and consumers swap implementations at runtime. The Service
// singleton in DfH is replaced by an explicit injectable accessor.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/log/Format.hpp>
#include <cd/log/LogLevel.hpp>
#include <cd/log/LogRecord.hpp>

#include <chrono>
#include <memory>
#include <source_location>
#include <string_view>

namespace cd::log
{

/// Observer hook for host-side log mirroring (e.g., editor log panel).
class ILogObserver
{
public:
    ILogObserver() noexcept = default;
    virtual ~ILogObserver() = default;
    ILogObserver(const ILogObserver&) = delete;
    ILogObserver& operator=(const ILogObserver&) = delete;
    ILogObserver(ILogObserver&&) = delete;
    ILogObserver& operator=(ILogObserver&&) = delete;

    virtual void on_log_record(const LogRecord& record) = 0;
};

class ILogger
{
public:
    ILogger() noexcept = default;
    virtual ~ILogger() = default;
    ILogger(const ILogger&) = delete;
    ILogger& operator=(const ILogger&) = delete;
    ILogger(ILogger&&) = delete;
    ILogger& operator=(ILogger&&) = delete;

    // --- Core logging API -----------------------------------------------------

    template <class... Args>
    void log(LogLevel level, const std::source_location& loc, std::string_view fmt, Args&&... args)
    {
        if (!should_log(level))
        {
            return;
        }
        try
        {
            auto msg = detail::format_braces(fmt, std::forward<Args>(args)...);
            log_impl(level, &loc, msg);
        }
        catch (...)
        {
            log_impl(LogLevel::Error, &loc, "Log format error");
        }
    }

    template <class... Args>
    void trace(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Trace, loc, f, std::forward<Args>(a)...);
    }

    template <class... Args>
    void debug(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Debug, loc, f, std::forward<Args>(a)...);
    }

    template <class... Args>
    void info(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Info, loc, f, std::forward<Args>(a)...);
    }

    template <class... Args>
    void warn(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Warning, loc, f, std::forward<Args>(a)...);
    }

    template <class... Args>
    void error(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Error, loc, f, std::forward<Args>(a)...);
    }

    template <class... Args>
    void critical(const std::source_location& loc, std::string_view f, Args&&... a)
    {
        log(LogLevel::Critical, loc, f, std::forward<Args>(a)...);
    }

    virtual void set_level(LogLevel level) noexcept = 0;
    [[nodiscard]] virtual LogLevel level() const noexcept = 0;

    virtual void flush() noexcept = 0;

    virtual void add_observer(ILogObserver* observer)
    {
        (void)observer;
    }

    virtual void remove_observer(ILogObserver* observer)
    {
        (void)observer;
    }

protected:
    [[nodiscard]] virtual bool should_log(LogLevel level) const noexcept = 0;
    virtual void log_impl(LogLevel level, const std::source_location* loc, std::string_view msg) = 0;
};

}  // namespace cd::log
