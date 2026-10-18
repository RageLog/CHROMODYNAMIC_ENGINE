// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpenXrStageOrigin3D.hpp
// Standalone Concrete C++23 Construct Class #155: `cd::scene::OpenXrStageOrigin3D`
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

// 155: XROrigin3D -> OpenXrStageOrigin3D
class OpenXrStageOrigin3D final
    : public ChromaConstructBase<155, ConstructCategory::Spatial3D, ConstructRole::Stage>
{
public:
    float world_scale_factor       { 1.0F };
    float snap_turn_Step_deg       { 30.0F };
    float teleport_max_range_m     { 12.0F };
    bool  guardian_boundary_visible { true };
    bool  floor_stage_aligned       { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpenXrStageOrigin3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XROrigin3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpenXrStageOrigin3D + TeleportLocomotion";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpenXrStageOrigin3D>(*this);
    }
};

} // namespace cd::scene
