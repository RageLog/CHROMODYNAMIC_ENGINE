// =============================================================================
// CHROMODYNAMIC — cd/log/LogRecord.hpp
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/log/LogLevel.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cd::log
{

/// Immutable snapshot of a single log line.
struct LogRecord
{
    std::chrono::steady_clock::time_point observed_at {};
    LogLevel level { LogLevel::Info };
    std::string message {};
    std::optional<std::string> file_path {};
    std::optional<std::uint_least32_t> line {};
};

}  // namespace cd::log
