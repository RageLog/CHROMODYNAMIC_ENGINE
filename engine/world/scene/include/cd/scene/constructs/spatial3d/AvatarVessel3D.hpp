// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/AvatarVessel3D.hpp
// Standalone Concrete C++23 Construct Class #117: `cd::scene::AvatarVessel3D`
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

// 117: CharacterBody3D -> AvatarVessel3D
class AvatarVessel3D final
    : public ChromaConstructBase<117, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    float walk_speed_mps        { 5.5F };
    float sprint_multiplier     { 1.65F };
    float jump_impulse          { 6.8F };
    float capsule_radius        { 0.35F };
    float capsule_height        { 1.80F };
    float max_slope_angle_deg   { 45.0F };
    bool  grounded              { true };
    bool  plasma_blaster_active { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AvatarVessel3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CharacterBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "17:44"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "AvatarVessel3D + CapsuleCollider + CharacterController";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<AvatarVessel3D>(*this);
    }
};

} // namespace cd::scene
