// =============================================================================
// CHROMODYNAMIC — cd/ui/Spinner.hpp
// Phase 62.A / Wave 230 — indeterminate progress spinner state.
//
// Counterpart to `ProgressBar` (Phase 57) for the case where total
// work is unknown. Spinner state is a single angle in radians,
// advanced by `tick(delta_time)`:
//
//   angle ≡ (angle + speed * dt) mod (2*pi)
//
// Renderer draws a rotating segment using `angle()`. `speed_rps()` is
// rotations per second; default = 1.0 (one full turn per second, the
// VS Code / Slack default).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::ui
{

class Spinner
{
public:
    static constexpr float kTau = 6.28318530717958647692F;

    void set_speed_rps(float rotations_per_second) noexcept
    {
        speed_rps_ = rotations_per_second;
    }

    void tick(float delta_seconds) noexcept
    {
        angle_ += speed_rps_ * kTau * delta_seconds;
        while (angle_ >= kTau) angle_ -= kTau;
        while (angle_ < 0.0F)  angle_ += kTau;
    }

    [[nodiscard]] float angle() const noexcept { return angle_; }
    [[nodiscard]] float speed_rps() const noexcept { return speed_rps_; }

    void reset() noexcept { angle_ = 0.0F; }

private:
    float speed_rps_ { 1.0F };
    float angle_     { 0.0F };
};

}  // namespace cd::ui
