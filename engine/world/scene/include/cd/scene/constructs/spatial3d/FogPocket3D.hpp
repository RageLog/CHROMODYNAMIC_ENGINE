// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/FogPocket3D.hpp
// Standalone Concrete C++23 Construct Class #84: `cd::scene::FogPocket3D`
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
class FogPocket3D final
    : public ChromaConstructBase<84, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float density_extinction { 0.035F };
    float henyey_greenstein_g { 0.65F };
    float edge_falloff_m     { 2.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "FogPocket3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "FogVolume"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:19"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "FogPocket3D + FroxelVolumetricFog";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<FogPocket3D>(*this);
    }
};

} // namespace cd::scene
