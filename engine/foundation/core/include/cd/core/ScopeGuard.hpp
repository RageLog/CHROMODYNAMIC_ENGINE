// =============================================================================
// CHROMODYNAMIC — cd/core/ScopeGuard.hpp
// Phase 23.C / Wave 188 — header-only RAII deferred cleanup.
//
// Foundation primitive every codebase eventually wants: "run this
// callable at scope exit unless dismiss() is called." Common shape
// for transactional code and Vulkan-style error paths that release
// half-built resources.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <utility>

namespace cd::core
{

template <class F>
class ScopeGuard
{
public:
    explicit ScopeGuard(F f) noexcept : f_ { std::move(f) } {}  // armed_ defaults to true

    ~ScopeGuard()
    {
        if (armed_) f_();
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& other) noexcept
        : f_ { std::move(other.f_) }, armed_ { other.armed_ }
    {
        other.armed_ = false;
    }
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    /// Cancel the cleanup. After dismiss(), the dtor does nothing.
    void dismiss() noexcept { armed_ = false; }
    [[nodiscard]] bool armed() const noexcept { return armed_; }

private:
    F f_;
    bool armed_ { true };
};

/// Deduction-friendly constructor wrapper.
template <class F>
[[nodiscard]] inline ScopeGuard<F> make_scope_guard(F f) noexcept
{
    return ScopeGuard<F> { std::move(f) };
}

}  // namespace cd::core
