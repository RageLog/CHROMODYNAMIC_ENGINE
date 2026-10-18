// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmSdfVolumeCollider3D.hpp
// Standalone Concrete C++23 Construct Class #94: `cd::scene::SwarmSdfVolumeCollider3D`
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
class SwarmSdfVolumeCollider3D final
    : public ChromaConstructBase<94, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t clipmap_voxel_res   { 64u };
    float         surface_thickness_m { 0.08F };
    bool          live_tlas_update    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmSdfVolumeCollider3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesCollisionSDF3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmSdfVolumeCollider3D + TlasSdfProbe";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmSdfVolumeCollider3D>(*this);
    }
};

} // namespace cd::scene
