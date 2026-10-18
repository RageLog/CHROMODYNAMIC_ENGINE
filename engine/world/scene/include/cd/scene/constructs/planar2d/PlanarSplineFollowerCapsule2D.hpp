// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSplineFollowerCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #59: `cd::scene::PlanarSplineFollowerCapsule2D`
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

// [59] PathFollow2D -> PlanarSplineFollowerCapsule2D (08:47)
class PlanarSplineFollowerCapsule2D final
    : public ChromaConstructBase<59, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float progress_ratio   { 0.0F };
    bool  rotate_to_tangent { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSplineFollowerCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PathFollow2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:47"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSplineFollowerCapsule2D + FrenetMover"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSplineFollowerCapsule2D>(*this); }
};

} // namespace cd::scene
