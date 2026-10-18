// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/SDFSmoothBooleanCSG3D.hpp
// Standalone Concrete C++23 Construct Class #226: `cd::scene::SDFSmoothBooleanCSG3D`
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

// 226: SDFSmoothBooleanCSG3D
class SDFSmoothBooleanCSG3D final
    : public ChromaConstructBase<226, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    float        smooth_blend_radius { 0.25F };
    std::uint8_t boolean_op_kind     { 0u };
    bool         extract_collider    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SDFSmoothBooleanCSG3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SDFSmoothBooleanCSG3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "SDFSmoothBooleanCSG3D + SmoothUnionSDF + CarraraMarble"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<SDFSmoothBooleanCSG3D>(*this); }
};

} // namespace cd::scene
