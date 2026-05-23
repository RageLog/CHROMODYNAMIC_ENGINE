// =============================================================================
// CHROMODYNAMIC — cd/time/FramePacer.hpp
// ADR-005 §B (multi-tier accumulator) — Fix Your Timestep (Glenn Fiedler 2018)
//
// Fixed simulation timestep + variable-rate render with interpolation alpha.
// Prevents spiral-of-death via max-substep cap. Designed to be poll-style:
// the game loop calls update(real_dt) once per frame and consumes the
// resulting (sim_steps, alpha) signal.
// =============================================================================
#pragma once

#include <cd/time/Types.hpp>

#include <algorithm>
#include <chrono>

namespace cd::time
{

struct FramePacerOptions
{
    Duration sim_step { std::chrono::duration_cast<Duration>(std::chrono::microseconds { 16'667 }) };
    std::uint32_t max_substeps { 4 };  // spiral-of-death cap
};

struct PaceResult
{
    std::uint32_t sim_steps { 0 };  // number of fixed simulation steps to run
    double alpha { 0.0 };           // interpolation factor in [0, 1) for render
    Duration leftover { Duration::zero() };
};

class FramePacer
{
public:
    explicit FramePacer(FramePacerOptions options = {}) noexcept
        : options_ { options }
    {
    }

    /// Feed the real-time delta since the previous call. Returns how many
    /// simulation substeps to execute and the render interpolation alpha.
    PaceResult update(Duration real_dt) noexcept
    {
        accumulator_ += real_dt;
        PaceResult r;
        while (accumulator_ >= options_.sim_step && r.sim_steps < options_.max_substeps)
        {
            accumulator_ -= options_.sim_step;
            ++r.sim_steps;
        }
        // Spiral-of-death cap: drop residual to avoid permanent backlog.
        if (accumulator_ >= options_.sim_step * options_.max_substeps)
        {
            accumulator_ = Duration::zero();
        }
        r.alpha = std::chrono::duration<double> { accumulator_ }.count() /
                  std::chrono::duration<double> { options_.sim_step }.count();
        r.leftover = accumulator_;
        return r;
    }

    void reset() noexcept
    {
        accumulator_ = Duration::zero();
    }

    [[nodiscard]] Duration accumulator() const noexcept
    {
        return accumulator_;
    }

    [[nodiscard]] const FramePacerOptions& options() const noexcept
    {
        return options_;
    }

    void set_options(FramePacerOptions opts) noexcept
    {
        options_ = opts;
    }

private:
    FramePacerOptions options_;
    Duration accumulator_ { Duration::zero() };
};

}  // namespace cd::time
