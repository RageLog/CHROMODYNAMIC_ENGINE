// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RagdollChain3D.hpp
// Standalone Concrete C++23 Construct Class #158: `cd::scene::RagdollChain3D`
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

// 158: PhysicalBoneSimulator3D -> RagdollChain3D
class RagdollChain3D final
    : public ChromaConstructBase<158, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    float         physical_blend_weight    { 0.0F };
    float         hit_reaction_recovery_hz { 3.5F };
    std::uint32_t xpbd_iterations          { 8u };
    bool          prune_self_collision     { true };
    bool          active_ragdoll_enabled   { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RagdollChain3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PhysicalBoneSimulator3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "41:25"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RagdollChain3D + ActiveRagdollBlend";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RagdollChain3D>(*this);
    }
};

} // namespace cd::scene
