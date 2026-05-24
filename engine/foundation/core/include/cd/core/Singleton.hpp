// =============================================================================
// CHROMODYNAMIC — cd/core/Singleton.hpp
// Phase 98.B / Wave 266 — CRTP single-instance helper.
//
// `cd::core::Singleton<Derived>` exposes `Derived::instance()` returning
// a thread-safe `static` reference. The Derived ctor is private; only
// `Singleton<Derived>` (a friend) can call it.
//
//   class Logger : public cd::core::Singleton<Logger> {
//     friend class cd::core::Singleton<Logger>;
//     Logger() = default;
//   public:
//     void log(std::string_view msg);
//   };
//
//   Logger::instance().log("hello");
//
// Use sparingly — singletons obscure dependency graphs. CHROMODYNAMIC
// prefers explicit context/registry injection where possible (CLAUDE.md
// §7). Singleton here is the escape hatch for "process-wide invariant
// resources" (e.g. ProfileRegistry).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::core
{

template <class Derived>
class Singleton
{
public:
    [[nodiscard]] static Derived& instance() noexcept
    {
        static Derived s;
        return s;
    }

    Singleton(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton& operator=(Singleton&&) = delete;

protected:
    Singleton() = default;
    ~Singleton() = default;
};

}  // namespace cd::core
