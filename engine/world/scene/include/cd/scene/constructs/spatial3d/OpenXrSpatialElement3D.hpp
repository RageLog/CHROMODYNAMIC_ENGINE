// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpenXrSpatialElement3D.hpp
// Standalone Concrete C++23 Construct Class #153: `cd::scene::OpenXrSpatialElement3D`
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

// 153: XRNode3D -> OpenXrSpatialElement3D
class OpenXrSpatialElement3D final
    : public ChromaConstructBase<153, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string action_pose_path        { "/user/hand/left/input/grip/pose" };
    float       prediction_lead_ms      { 11.1F };
    bool        desktop_6dof_simulator  { true };
    bool        late_latching_enabled   { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpenXrSpatialElement3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XRNode3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpenXrSpatialElement3D + DesktopXrSim";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpenXrSpatialElement3D>(*this);
    }
};

} // namespace cd::scene
