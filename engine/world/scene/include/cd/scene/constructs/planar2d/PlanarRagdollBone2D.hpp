// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarRagdollBone2D.hpp
// Standalone Concrete C++23 Construct Class #66: `cd::scene::PlanarRagdollBone2D`
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

// [66] PhysicalBone2D -> PlanarRagdollBone2D (09:42)
class PlanarRagdollBone2D final
    : public ChromaConstructBase<66, ConstructCategory::Planar2D, ConstructRole::Vessel>
{
public:
    float ragdoll_blend_weight { 0.5F };
    float pd_stiffness         { 120.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarRagdollBone2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PhysicalBone2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "09:42"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarRagdollBone2D + ActiveRagdoll"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarRagdollBone2D>(*this); }
};

} // namespace cd::scene
