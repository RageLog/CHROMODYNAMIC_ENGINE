// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RenderableSurfaceElement3D.hpp
// Standalone Concrete C++23 Construct Class #74: `cd::scene::RenderableSurfaceElement3D`
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
class RenderableSurfaceElement3D final
    : public ChromaConstructBase<74, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t bindless_material_slot { 2u };
    float         lod_bias               { 1.0F };
    bool          include_in_tlas        { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RenderableSurfaceElement3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GeometryInstance3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "11:26"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RenderableSurfaceElement3D + BindlessTex";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RenderableSurfaceElement3D>(*this);
    }
};

} // namespace cd::scene
