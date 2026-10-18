// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/HiZSimulationGateCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #139: `cd::scene::HiZSimulationGateCapsule3D`
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

// 139: VisibleOnScreenEnabler3D -> HiZSimulationGateCapsule3D
class HiZSimulationGateCapsule3D final
    : public ChromaConstructBase<139, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         full_rate_distance_m   { 25.0F };
    float         sleep_distance_m       { 95.0F };
    std::uint32_t occluded_tick_divisor  { 4u };
    bool          throttle_physics       { true };
    bool          throttle_skeletal_anim { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "HiZSimulationGateCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VisibleOnScreenEnabler3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "22:20"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "HiZSimulationGateCapsule3D + TickLOD";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<HiZSimulationGateCapsule3D>(*this);
    }
};

} // namespace cd::scene
