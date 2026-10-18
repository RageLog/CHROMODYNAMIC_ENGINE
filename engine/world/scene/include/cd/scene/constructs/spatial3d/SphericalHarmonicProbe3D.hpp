// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SphericalHarmonicProbe3D.hpp
// Standalone Concrete C++23 Construct Class #136: `cd::scene::SphericalHarmonicProbe3D`
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

// 136: LightmapProbe -> SphericalHarmonicProbe3D
class SphericalHarmonicProbe3D final
    : public ChromaConstructBase<136, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 27> l2_sh_coefficients {};
    float                 influence_radius_m { 4.5F };
    bool                  tetrahedral_interp { true };
    bool                  auto_navmesh_place { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SphericalHarmonicProbe3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "LightmapProbe"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "21:30"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SphericalHarmonicProbe3D + L2SH";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SphericalHarmonicProbe3D>(*this);
    }
};

} // namespace cd::scene
