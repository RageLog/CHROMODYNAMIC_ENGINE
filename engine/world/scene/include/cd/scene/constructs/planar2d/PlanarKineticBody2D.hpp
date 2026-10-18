// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarKineticBody2D.hpp
// Standalone Concrete C++23 Construct Class #31: `cd::scene::PlanarKineticBody2D`
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

// [31] RigidBody2D -> PlanarKineticBody2D (04:08)
class PlanarKineticBody2D final
    : public ChromaConstructBase<31, ConstructCategory::Planar2D, ConstructRole::Vessel>
{
public:
    float mass_kg       { 1.0F };
    float gravity_scale { 1.0F };
    bool  continuous_cd { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarKineticBody2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RigidBody2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "04:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarKineticBody2D + DynamicCCD"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarKineticBody2D>(*this); }
};

} // namespace cd::scene
