// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarAvatarVessel2D.hpp
// Standalone Concrete C++23 Construct Class #32: `cd::scene::PlanarAvatarVessel2D`
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

// [32] CharacterBody2D -> PlanarAvatarVessel2D (04:19)
class PlanarAvatarVessel2D final
    : public ChromaConstructBase<32, ConstructCategory::Planar2D, ConstructRole::Vessel>
{
public:
    float move_speed      { 220.0F };
    float jump_velocity   { 420.0F };
    float coyote_time_sec { 0.12F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarAvatarVessel2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CharacterBody2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "04:19"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarAvatarVessel2D + CoyoteJump"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarAvatarVessel2D>(*this); }
};

} // namespace cd::scene
