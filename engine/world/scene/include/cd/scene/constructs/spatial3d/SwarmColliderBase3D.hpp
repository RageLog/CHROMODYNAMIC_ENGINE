// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmColliderBase3D.hpp
// Standalone Concrete C++23 Construct Class #91: `cd::scene::SwarmColliderBase3D`
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
class SwarmColliderBase3D final
    : public ChromaConstructBase<91, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         restitution_bounce  { 0.72F };
    float         coulomb_friction    { 0.18F };
    std::uint32_t collision_layer_bit { 1u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmColliderBase3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesCollision3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmColliderBase3D + SharedBroadphase";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmColliderBase3D>(*this);
    }
};

} // namespace cd::scene
