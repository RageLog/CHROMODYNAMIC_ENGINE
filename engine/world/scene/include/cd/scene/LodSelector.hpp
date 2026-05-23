// =============================================================================
// CHROMODYNAMIC — cd/scene/LodSelector.hpp
// Phase 19.G / Wave 180 — header-only distance-LOD selector.
//
// Stores a sorted array of distance thresholds. Given a camera-relative
// distance, returns the index of the LOD that should be active at
// that distance: 0 = closest mesh, N-1 = farthest. A `force()` override
// lets editor tools pin an LOD for inspection.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::scene
{

class LodSelector
{
public:
    /// Construct from a sorted ascending list of distance thresholds.
    /// thresholds.size() = number of LODs - 1.
    /// Example: {5.0, 20.0} → LOD0 at d<5, LOD1 at 5<=d<20, LOD2 at d>=20.
    explicit LodSelector(std::vector<float> thresholds) noexcept
        : thresholds_ { std::move(thresholds) }
    {
    }

    [[nodiscard]] std::size_t lod_count() const noexcept
    {
        return thresholds_.size() + 1;
    }

    /// Returns the active LOD index given a camera-relative distance.
    /// When `forced()` is set, returns that index instead.
    [[nodiscard]] std::size_t select(float distance) const noexcept
    {
        if (forced_ >= 0)
            return static_cast<std::size_t>(forced_);
        std::size_t i = 0;
        for (; i < thresholds_.size(); ++i)
            if (distance < thresholds_[i])
                return i;
        return thresholds_.size();
    }

    void force(std::size_t lod) noexcept { forced_ = static_cast<int>(lod); }
    void unforce() noexcept { forced_ = -1; }
    [[nodiscard]] bool is_forced() const noexcept { return forced_ >= 0; }

private:
    std::vector<float> thresholds_;
    int forced_ { -1 };
};

}  // namespace cd::scene
