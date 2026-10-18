// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RadianceProbe3D.hpp
// Standalone Concrete C++23 Construct Class #137: `cd::scene::RadianceProbe3D`
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

// 137: ReflectionProbe -> RadianceProbe3D
class RadianceProbe3D final
    : public ChromaConstructBase<137, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> box_extents_m          { 8.0F, 4.0F, 8.0F };
    std::uint32_t        cubemap_face_res       { 256u };
    float                reflection_intensity   { 1.0F };
    float                ray_query_rough_cutoff { 0.35F };
    bool                 box_parallax_correct   { true };
    bool                 hardware_ray_query     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RadianceProbe3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ReflectionProbe"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "21:44"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RadianceProbe3D + HardwareRayQueryMirror";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RadianceProbe3D>(*this);
    }
};

} // namespace cd::scene
