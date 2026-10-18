// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarCollisionElement2D.hpp
// Standalone Concrete C++23 Construct Class #25: `cd::scene::PlanarCollisionElement2D`
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

// [25] CollisionObject2D -> PlanarCollisionElement2D (03:07)
class PlanarCollisionElement2D final
    : public ChromaConstructBase<25, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t collision_layer { 1u };
    std::uint32_t collision_mask  { 1u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarCollisionElement2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionObject2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:07"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarCollisionElement2D + SpatialHash"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarCollisionElement2D>(*this); }
};

} // namespace cd::scene
