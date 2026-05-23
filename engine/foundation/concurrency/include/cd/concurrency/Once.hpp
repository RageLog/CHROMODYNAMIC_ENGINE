// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Once.hpp
// Phase 26.D / Wave 194 — single-shot initialization primitive.
//
// Header-only `std::call_once` wrapper that exposes a richer API
// (`is_done()`, `reset_for_test()`) than std::once_flag alone. Used
// for lazy initialization of singletons (CVar registry, logger sinks)
// where double-checked locking would otherwise be re-rolled per call
// site.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <mutex>
#include <new>
#include <utility>

namespace cd::concurrency
{

class Once
{
public:
    /// Run `f` exactly once, even under concurrent invocation.
    template <class F>
    void call(F&& f)
    {
        std::call_once(flag_, std::forward<F>(f));
        done_ = true;
    }

    [[nodiscard]] bool is_done() const noexcept { return done_; }

    /// Test-only: reset the flag so a subsequent call() will run f
    /// again. Not synchronized; do not use under contention.
    void reset_for_test() noexcept
    {
        // std::once_flag isn't copy/move-assignable; placement-new
        // is the standard reinit trick.
        flag_.~once_flag();
        new (&flag_) std::once_flag {};
        done_ = false;
    }

private:
    std::once_flag flag_;
    bool done_ { false };
};

}  // namespace cd::concurrency
