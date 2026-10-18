// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/VisualRadianceElement3D.hpp
// Standalone Concrete C++23 Construct Class #73: `cd::scene::VisualRadianceElement3D`
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

// -----------------------------------------------------------------------------
class VisualRadianceElement3D final
    : public ChromaConstructBase<73, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t visibility_layer_mask { 0x0000'0001u };
    float         fade_begin_m          { 80.0F };
    float         fade_end_m            { 120.0F };
    bool          rt_shadow_caster      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "VisualRadianceElement3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VisualInstance3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "11:09"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "VisualRadianceElement3D + BlasInstance";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<VisualRadianceElement3D>(*this);
    }
};

} // namespace cd::scene
