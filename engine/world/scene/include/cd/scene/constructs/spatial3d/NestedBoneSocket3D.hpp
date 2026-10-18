// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/NestedBoneSocket3D.hpp
// Standalone Concrete C++23 Construct Class #130: `cd::scene::NestedBoneSocket3D`
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

// 130: BoneAttachment3D -> NestedBoneSocket3D
class NestedBoneSocket3D final
    : public ChromaConstructBase<130, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string          socket_bone_name { "hand_r" };
    std::uint32_t        bone_palette_idx { 14u };
    std::array<float, 3> socket_offset_m  { 0.0F, 0.08F, 0.0F };
    bool                 override_pose    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NestedBoneSocket3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "BoneAttachment3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "20:31"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "NestedBoneSocket3D + BoneMatrixSocket";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<NestedBoneSocket3D>(*this);
    }
};

} // namespace cd::scene
