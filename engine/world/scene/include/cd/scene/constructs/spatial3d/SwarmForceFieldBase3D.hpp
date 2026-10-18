// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmForceFieldBase3D.hpp
// Standalone Concrete C++23 Construct Class #87: `cd::scene::SwarmForceFieldBase3D`
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
class SwarmForceFieldBase3D final
    : public ChromaConstructBase<87, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float force_newton_scale     { 18.0F };
    float attenuation_exp        { 1.5F };
    bool  affect_rigid_bodies    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmForceFieldBase3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesAttractor3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmForceFieldBase3D + UnifiedForce";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmForceFieldBase3D>(*this);
    }
};

} // namespace cd::scene
