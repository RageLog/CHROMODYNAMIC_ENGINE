// =============================================================================
// CHROMODYNAMIC — cd/core/ProfileSpan.hpp
// Phase 51.B / Wave 219 — RAII scope timer with named counter accumulator.
//
// `ProfileSpan` is a header-only telemetry primitive for fast in-engine
// timing on named scopes. The accumulator is a global `ProfileRegistry`
// keyed by const-char string; each span constructor stashes the start
// timestamp, and the destructor adds the elapsed nanoseconds to the
// registry's running sum + count for that name.
//
// Use:
//   {
//       cd::core::ProfileSpan _span { "render_pass" };
//       // ... work ...
//   }  // → registry["render_pass"] gets +elapsed_ns, ++count
//
// `ProfileRegistry::stats(name)` returns (total_ns, count). The
// registry uses an internal mutex; correctness over throughput for
// the editor use case.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cd::core
{

struct ProfileStats
{
    std::uint64_t total_ns { 0 };
    std::uint64_t count    { 0 };
};

class ProfileRegistry
{
public:
    static ProfileRegistry& instance() noexcept
    {
        static ProfileRegistry r;
        return r;
    }

    void record(const char* name, std::uint64_t elapsed_ns) noexcept
    {
        std::scoped_lock lock { mu_ };
        auto& s = entries_[name];
        s.total_ns += elapsed_ns;
        ++s.count;
    }

    [[nodiscard]] ProfileStats stats(const char* name) const
    {
        std::scoped_lock lock { mu_ };
        const auto it = entries_.find(name);
        return (it != entries_.end()) ? it->second : ProfileStats {};
    }

    void clear() noexcept
    {
        std::scoped_lock lock { mu_ };
        entries_.clear();
    }

    [[nodiscard]] std::size_t size() const
    {
        std::scoped_lock lock { mu_ };
        return entries_.size();
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, ProfileStats> entries_;
};

class ProfileSpan
{
public:
    explicit ProfileSpan(const char* name) noexcept
        : name_ { name }, start_ { std::chrono::steady_clock::now() } {}

    ProfileSpan(const ProfileSpan&) = delete;
    ProfileSpan& operator=(const ProfileSpan&) = delete;

    ~ProfileSpan() noexcept
    {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        ProfileRegistry::instance().record(name_, static_cast<std::uint64_t>(ns));
    }

private:
    const char*                                    name_;
    std::chrono::steady_clock::time_point          start_;
};

}  // namespace cd::core
