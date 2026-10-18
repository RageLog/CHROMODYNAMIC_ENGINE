// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SolarLight3D.hpp
// Standalone Concrete C++23 Construct Class #99: `cd::scene::SolarLight3D`
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
class SolarLight3D final
    : public ChromaConstructBase<99, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         illuminance_lux     { 110000.0F };
    float         sun_angular_diam_deg { 0.53F };
    std::uint32_t csm_cascade_count   { 4u };
    bool          inline_ray_shadows  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SolarLight3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "DirectionalLight3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "14:54"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SolarLight3D + CSM + RayQueryShadows";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SolarLight3D>(*this);
    }
};

} // namespace cd::scene
