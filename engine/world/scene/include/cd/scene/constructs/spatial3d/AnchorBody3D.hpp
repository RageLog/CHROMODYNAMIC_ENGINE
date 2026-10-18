// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/AnchorBody3D.hpp
// Standalone Concrete C++23 Construct Class #114: `cd::scene::AnchorBody3D`
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
class AnchorBody3D final
    : public ChromaConstructBase<114, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float surface_friction    { 0.80F };
    float surface_restitution { 0.10F };
    bool  navmesh_walkable    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AnchorBody3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "StaticBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "16:52"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "AnchorBody3D + StaticRigidBody + BoxCollider";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<AnchorBody3D>(*this);
    }
};

} // namespace cd::scene
