// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/ProjectionDecal3D.hpp
// Standalone Concrete C++23 Construct Class #79: `cd::scene::ProjectionDecal3D`
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
class ProjectionDecal3D final
    : public ChromaConstructBase<79, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> obb_half_extents   { 0.7F, 0.25F, 0.7F };
    float                normal_angle_fade  { 0.65F };
    float                albedo_mix         { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ProjectionDecal3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Decal"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "12:30"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "ProjectionDecal3D + ObbDepthProject";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<ProjectionDecal3D>(*this);
    }
};

} // namespace cd::scene
