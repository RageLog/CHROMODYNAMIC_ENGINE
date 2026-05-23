// =============================================================================
// CHROMODYNAMIC — cd/log/Service.hpp
// Thread-safe global accessor for the active ILogger. Singleton-free in spirit:
// hosts can call `Service::clear()` and inject a different logger at any time.
//
// Use macros (CD_LOG_*) for compile-time-stripping convenience or call
// `cd::log::get()` directly for full control.
// =============================================================================
#pragma once

#include <cd/log/ILogger.hpp>

#include <atomic>
#include <memory>
#include <source_location>
#include <utility>

namespace cd::log
{

namespace detail
{

inline std::shared_ptr<ILogger>& global_slot() noexcept
{
    // Slot held inside a function-local for deterministic destruction order
    // (`static` inside .cpp file would race with TUs registering loggers).
    static std::shared_ptr<ILogger> s;
    return s;
}

inline std::atomic<ILogger*>& global_raw_slot() noexcept
{
    static std::atomic<ILogger*> s { nullptr };
    return s;
}

}  // namespace detail

inline void set(std::shared_ptr<ILogger> logger) noexcept
{
    detail::global_slot() = logger;
    detail::global_raw_slot().store(detail::global_slot().get(), std::memory_order_release);
}

inline void clear() noexcept
{
    detail::global_raw_slot().store(nullptr, std::memory_order_release);
    detail::global_slot().reset();
}

[[nodiscard]] inline ILogger* get() noexcept
{
    return detail::global_raw_slot().load(std::memory_order_acquire);
}

[[nodiscard]] inline bool available() noexcept
{
    return get() != nullptr;
}

}  // namespace cd::log

// --- Convenience macros ---------------------------------------------------
// All macros are noexcept-safe; if no logger is registered they are no-ops.

#define CD_LOG_TRACE(...)                                               \
    do                                                                  \
    {                                                                   \
        if (auto* _cd_l = ::cd::log::get())                             \
        {                                                               \
            _cd_l->trace(std::source_location::current(), __VA_ARGS__); \
        }                                                               \
    }                                                                   \
    while (0)

#define CD_LOG_DEBUG(...)                                               \
    do                                                                  \
    {                                                                   \
        if (auto* _cd_l = ::cd::log::get())                             \
        {                                                               \
            _cd_l->debug(std::source_location::current(), __VA_ARGS__); \
        }                                                               \
    }                                                                   \
    while (0)

#define CD_LOG_INFO(...)                                               \
    do                                                                 \
    {                                                                  \
        if (auto* _cd_l = ::cd::log::get())                            \
        {                                                              \
            _cd_l->info(std::source_location::current(), __VA_ARGS__); \
        }                                                              \
    }                                                                  \
    while (0)

#define CD_LOG_WARN(...)                                               \
    do                                                                 \
    {                                                                  \
        if (auto* _cd_l = ::cd::log::get())                            \
        {                                                              \
            _cd_l->warn(std::source_location::current(), __VA_ARGS__); \
        }                                                              \
    }                                                                  \
    while (0)

#define CD_LOG_ERROR(...)                                               \
    do                                                                  \
    {                                                                   \
        if (auto* _cd_l = ::cd::log::get())                             \
        {                                                               \
            _cd_l->error(std::source_location::current(), __VA_ARGS__); \
        }                                                               \
    }                                                                   \
    while (0)

#define CD_LOG_CRITICAL(...)                                               \
    do                                                                     \
    {                                                                      \
        if (auto* _cd_l = ::cd::log::get())                                \
        {                                                                  \
            _cd_l->critical(std::source_location::current(), __VA_ARGS__); \
        }                                                                  \
    }                                                                      \
    while (0)
