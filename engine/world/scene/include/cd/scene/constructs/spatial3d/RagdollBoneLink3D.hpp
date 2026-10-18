// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RagdollBoneLink3D.hpp
// Standalone Concrete C++23 Construct Class #131: `cd::scene::RagdollBoneLink3D`
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

// 131: PhysicalBone3D -> RagdollBoneLink3D
class RagdollBoneLink3D final
    : public ChromaConstructBase<131, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    std::uint32_t bone_index          { 0u };
    float         link_mass_kg        { 3.5F };
    float         capsule_radius_m    { 0.09F };
    float         capsule_length_m    { 0.38F };
    float         joint_damping       { 0.25F };
    bool          auto_fit_to_bone    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RagdollBoneLink3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PhysicalBone3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "41:25"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RagdollBoneLink3D + AutoFitCapsule";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RagdollBoneLink3D>(*this);
    }
};

} // namespace cd::scene
