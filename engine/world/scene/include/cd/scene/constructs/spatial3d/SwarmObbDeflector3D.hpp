// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmObbDeflector3D.hpp
// Standalone Concrete C++23 Construct Class #92: `cd::scene::SwarmObbDeflector3D`
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
class SwarmObbDeflector3D final
    : public ChromaConstructBase<92, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> half_extents_m     { 0.75F, 0.2F, 0.75F };
    float                deflection_elastic { 0.80F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmObbDeflector3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesCollisionBox3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmObbDeflector3D + StaticOBB";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmObbDeflector3D>(*this);
    }
};

} // namespace cd::scene
