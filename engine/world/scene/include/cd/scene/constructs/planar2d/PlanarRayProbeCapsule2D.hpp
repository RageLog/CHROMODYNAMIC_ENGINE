// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarRayProbeCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #61: `cd::scene::PlanarRayProbeCapsule2D`
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

// [61] RayCast2D -> PlanarRayProbeCapsule2D (09:07)
class PlanarRayProbeCapsule2D final
    : public ChromaConstructBase<61, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float target_distance_px { 128.0F };
    bool  hit_from_inside    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarRayProbeCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RayCast2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "09:07"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarRayProbeCapsule2D + SimdRayCast"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarRayProbeCapsule2D>(*this); }
};

} // namespace cd::scene
