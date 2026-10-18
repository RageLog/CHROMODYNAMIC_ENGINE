// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SpatialCollisionElement3D.hpp
// Standalone Concrete C++23 Construct Class #110: `cd::scene::SpatialCollisionElement3D`
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

// -----------------------------------------------------------------------------
class SpatialCollisionElement3D final
    : public ChromaConstructBase<110, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t collision_layer { 1u };
    std::uint32_t collision_mask  { 1u };
    bool          input_ray_pick  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SpatialCollisionElement3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionObject3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:54"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SpatialCollisionElement3D + InlineCollider";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SpatialCollisionElement3D>(*this);
    }
};

} // namespace cd::scene
