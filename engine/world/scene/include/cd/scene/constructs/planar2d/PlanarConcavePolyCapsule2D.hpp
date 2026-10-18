// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarConcavePolyCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #28: `cd::scene::PlanarConcavePolyCapsule2D`
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

// [28] CollisionPolygon2D -> PlanarConcavePolyCapsule2D (03:25)
class PlanarConcavePolyCapsule2D final
    : public ChromaConstructBase<28, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    std::uint32_t convex_hull_count  { 4u };
    float         alpha_threshold    { 0.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarConcavePolyCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionPolygon2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:25"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarConcavePolyCapsule2D + ConvexDecomp"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarConcavePolyCapsule2D>(*this); }
};

} // namespace cd::scene
