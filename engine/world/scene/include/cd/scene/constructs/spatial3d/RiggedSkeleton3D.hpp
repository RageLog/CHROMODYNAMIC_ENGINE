// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RiggedSkeleton3D.hpp
// Standalone Concrete C++23 Construct Class #129: `cd::scene::RiggedSkeleton3D`
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

// 129: Skeleton3D -> RiggedSkeleton3D
class RiggedSkeleton3D final
    : public ChromaConstructBase<129, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string   gltf_asset_path    { "assets/models/CesiumMan.glb" };
    std::uint32_t joint_count        { 64u };
    float         playback_speed     { 1.0F };
    float         timeline_scrub_sec { 0.0F };
    bool          gpu_skinning       { true };
    bool          dynamic_blas_refit { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RiggedSkeleton3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Skeleton3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "20:11"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RiggedSkeleton3D + DynamicBlasRefit";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RiggedSkeleton3D>(*this);
    }
};

} // namespace cd::scene
