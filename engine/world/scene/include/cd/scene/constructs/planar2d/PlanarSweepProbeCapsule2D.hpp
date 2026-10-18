// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSweepProbeCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #62: `cd::scene::PlanarSweepProbeCapsule2D`
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

// [62] ShapeCast2D -> PlanarSweepProbeCapsule2D (09:16)
class PlanarSweepProbeCapsule2D final
    : public ChromaConstructBase<62, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float         sweep_radius_px { 16.0F };
    std::uint32_t max_results     { 8u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSweepProbeCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ShapeCast2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "09:16"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSweepProbeCapsule2D + GjkSweep"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSweepProbeCapsule2D>(*this); }
};

} // namespace cd::scene
