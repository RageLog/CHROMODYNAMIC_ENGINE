// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/ClothSurface3D.hpp
// Standalone Concrete C++23 Construct Class #109: `cd::scene::ClothSurface3D`
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
class ClothSurface3D final
    : public ChromaConstructBase<109, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    std::uint32_t xpbd_substeps     { 8u };
    float         compliance        { 0.0001F };
    float         aerodynamic_drag  { 0.15F };
    bool          self_collision_on { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ClothSurface3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SoftBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "ClothSurface3D + XPBDSoftBody + Iridescence";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<ClothSurface3D>(*this);
    }
};

} // namespace cd::scene
