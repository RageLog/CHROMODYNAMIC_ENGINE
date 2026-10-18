// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/KinematicMoverElement3D.hpp
// Standalone Concrete C++23 Construct Class #115: `cd::scene::KinematicMoverElement3D`
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

// 115: AnimatableBody3D -> KinematicMoverElement3D
class KinematicMoverElement3D final
    : public ChromaConstructBase<115, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> move_axis        { 1.0F, 0.0F, 0.0F };
    float                amplitude        { 2.5F };
    float                frequency_hz     { 0.5F };
    float                spin_deg_per_sec { 0.0F };
    bool                 carry_riders     { true };
    std::array<float, 3> last_frame_delta { 0.0F, 0.0F, 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "KinematicMoverElement3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimatableBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "17:16"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "KinematicMoverElement3D + CarryRiders + CarbonPBR";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<KinematicMoverElement3D>(*this);
    }

    void on_tick(Element& host, float dt) noexcept override
    {
        const float prev_t = elapsed_time_;
        IChromaConstruct::on_tick(host, dt);
        if (!enabled_ || dt <= 0.0F)
        {
            return;
        }
        constexpr float kTwoPi = 6.28318530718F;
        const float prev_offset = std::sin(prev_t * frequency_hz * kTwoPi) * amplitude;
        const float curr_offset = std::sin(elapsed_time_ * frequency_hz * kTwoPi) * amplitude;
        const float delta       = curr_offset - prev_offset;
        for (std::size_t i = 0; i < 3; ++i)
        {
            last_frame_delta.at(i) = move_axis.at(i) * delta;
            host.local_pos[i] += last_frame_delta.at(i);
        }
    }
};

} // namespace cd::scene
