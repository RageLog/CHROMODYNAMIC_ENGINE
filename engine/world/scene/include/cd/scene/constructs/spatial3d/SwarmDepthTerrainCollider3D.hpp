// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmDepthTerrainCollider3D.hpp
// Standalone Concrete C++23 Construct Class #95: `cd::scene::SwarmDepthTerrainCollider3D`
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
class SwarmDepthTerrainCollider3D final
    : public ChromaConstructBase<95, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float ortho_capture_span_m { 64.0F };
    float depth_bias_m         { 0.02F };
    bool  reconstruct_normals  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmDepthTerrainCollider3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesCollisionHeightField3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmDepthTerrainCollider3D + DepthCollide";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmDepthTerrainCollider3D>(*this);
    }
};

} // namespace cd::scene
