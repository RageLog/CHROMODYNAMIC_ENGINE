// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SensorVolume3D.hpp
// Standalone Concrete C++23 Construct Class #118: `cd::scene::SensorVolume3D`
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

// 118: Area3D -> SensorVolume3D
class SensorVolume3D final
    : public ChromaConstructBase<118, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t trigger_preset_kind { 0u };
    float         impulse_boost_mps   { 12.0F };
    float         health_delta        { 25.0F };
    std::int32_t  score_delta         { 100 };
    bool          one_shot_trigger    { false };
    std::uint32_t overlap_count       { 0u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SensorVolume3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Area3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "17:58"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SensorVolume3D + TriggerVolumeModifier";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SensorVolume3D>(*this);
    }
};

} // namespace cd::scene
