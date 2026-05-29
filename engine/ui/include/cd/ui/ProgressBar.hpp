// =============================================================================
// CHROMODYNAMIC — cd/ui/ProgressBar.hpp
// Phase 56.A / Wave 224 — deterministic progress-state primitive.
//
// `ProgressBar` is the headless state for a loading / baking / streaming
// progress widget:
//   * `set_total(n)` — declare total work units.
//   * `tick(units=1)` — advance `done` by units (clamps at total).
//   * `progress()` — float ∈ [0,1].
//   * `is_complete()` — done >= total.
//   * `reset()` — back to 0.
//
// Rendering is the caller's job (ImGui ProgressBar, terminal bar,
// custom-rendered rect). The state machine is pure data so it tests
// trivially and works across rendering backends.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::ui
{

class ProgressBar
{
public:
    void set_total(std::uint64_t total) noexcept
    {
        total_ = total;
        done_ = std::min(done_, total_);
    }

    void tick(std::uint64_t units = 1) noexcept
    {
        done_ = std::min(done_ + units, total_);
    }

    void set_done(std::uint64_t done) noexcept
    {
        done_ = std::min(done, total_);
    }

    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }
    [[nodiscard]] std::uint64_t done() const noexcept { return done_; }

    [[nodiscard]] float progress() const noexcept
    {
        return (total_ == 0)
            ? 0.0F
            : static_cast<float>(done_) / static_cast<float>(total_);
    }

    [[nodiscard]] bool is_complete() const noexcept
    {
        return total_ > 0 && done_ >= total_;
    }

    void reset() noexcept { done_ = 0; }

private:
    std::uint64_t total_ { 0 };
    std::uint64_t done_  { 0 };
};

}  // namespace cd::ui
