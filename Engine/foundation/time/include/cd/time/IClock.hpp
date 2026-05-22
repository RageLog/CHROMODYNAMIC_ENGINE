// =============================================================================
// CHROMODYNAMIC — cd/time/IClock.hpp
// ADR-005 §F + ADR-017 P0 (DfH timing/iclock.hpp salvage)
//
// Injectable clock abstraction. Production code uses SteadyClock; tests
// inject mock clocks to deterministically advance time without sleeping.
// =============================================================================
#pragma once

#include <cd/time/Types.hpp>

namespace cd::time
{

class IClock
{
public:
    IClock() noexcept = default;
    virtual ~IClock() = default;

    IClock(const IClock&) = delete;
    IClock& operator=(const IClock&) = delete;
    IClock(IClock&&) = delete;
    IClock& operator=(IClock&&) = delete;

    [[nodiscard]] virtual TimePoint now() const noexcept = 0;
    [[nodiscard]] virtual TimeMode mode() const noexcept = 0;

    [[nodiscard]] bool is_simulation() const noexcept
    {
        return mode() == TimeMode::Simulation;
    }

    template <class ToDuration = Duration>
    [[nodiscard]] ToDuration now_as() const noexcept
    {
        return since_epoch<ToDuration>(now());
    }
};

}  // namespace cd::time
