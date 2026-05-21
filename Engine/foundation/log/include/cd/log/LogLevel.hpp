// =============================================================================
// CHROMODYNAMIC — cd/log/LogLevel.hpp
// ADR-013 + ADR-017 P1 (DfH logging/ilogger.hpp salvage)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

namespace cd::log
{

enum class LogLevel : std::uint8_t
{
    Trace = 0,  ///< Extremely detailed internal state (highest volume).
    Debug,      ///< Diagnostic information for developers.
    Info,       ///< General operational events (startup, shutdown, connection).
    Warning,    ///< Potential issues that do not stop execution (retries, timeouts).
    Error,      ///< Errors that affect functionality but allow the system to continue.
    Critical,   ///< Fatal errors that may require immediate shutdown.
    Off         ///< Disable logging.
};

[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warning:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Critical:
            return "critical";
        case LogLevel::Off:
            return "off";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view to_short_string(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::Trace:
            return "TRC";
        case LogLevel::Debug:
            return "DBG";
        case LogLevel::Info:
            return "INF";
        case LogLevel::Warning:
            return "WRN";
        case LogLevel::Error:
            return "ERR";
        case LogLevel::Critical:
            return "CRT";
        case LogLevel::Off:
            return "OFF";
    }
    return "???";
}

}  // namespace cd::log
