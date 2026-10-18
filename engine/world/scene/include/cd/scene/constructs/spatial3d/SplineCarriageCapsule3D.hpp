// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SplineCarriageCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #149: `cd::scene::SplineCarriageCapsule3D`
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

// 149: PathFollow3D -> SplineCarriageCapsule3D
class SplineCarriageCapsule3D final
    : public ChromaConstructBase<149, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float distance_along_track_m { 0.0F };
    float track_length_m         { 24.0F };
    float carriage_speed_mps     { 4.5F };
    bool  loop_around            { true };
    bool  ping_pong_reverse      { false };
    bool  align_to_tangent       { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SplineCarriageCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PathFollow3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:03"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SplineCarriageCapsule3D + MovingCarriage";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SplineCarriageCapsule3D>(*this);
    }

    void on_tick(Element& host, float dt) noexcept override
    {
        IChromaConstruct::on_tick(host, dt);
        if (!enabled_ || dt <= 0.0F || track_length_m <= 0.0F)
        {
            return;
        }
        distance_along_track_m += carriage_speed_mps * dt * host.world_time_scale;
        if (loop_around && distance_along_track_m >= track_length_m)
        {
            distance_along_track_m = std::fmod(distance_along_track_m, track_length_m);
        }
    }
};

} // namespace cd::scene
