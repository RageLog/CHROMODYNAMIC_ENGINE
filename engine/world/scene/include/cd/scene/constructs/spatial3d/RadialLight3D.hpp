// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RadialLight3D.hpp
// Standalone Concrete C++23 Construct Class #97: `cd::scene::RadialLight3D`
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
class RadialLight3D final
    : public ChromaConstructBase<97, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float intensity_lumens { 1200.0F };
    float kelvin_cct       { 4500.0F };
    float radius_m         { 8.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RadialLight3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "OmniLight3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "14:09"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RadialLight3D + ClusteredPointLight";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RadialLight3D>(*this);
    }
};

} // namespace cd::scene
