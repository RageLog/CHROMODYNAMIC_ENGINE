// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RootTrajectoryVisualizer3D.hpp
// Standalone Concrete C++23 Construct Class #133: `cd::scene::RootTrajectoryVisualizer3D`
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

// 133: RootMotionView -> RootTrajectoryVisualizer3D
class RootTrajectoryVisualizer3D final
    : public ChromaConstructBase<133, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         prediction_horizon_sec { 1.5F };
    float         history_window_sec     { 2.0F };
    float         grid_cell_size_m       { 0.5F };
    std::uint32_t ring_buffer_samples    { 60u };
    bool          show_footstep_contacts { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RootTrajectoryVisualizer3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RootMotionView"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "20:45"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RootTrajectoryVisualizer3D + TrajectoryArc";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RootTrajectoryVisualizer3D>(*this);
    }
};

} // namespace cd::scene
