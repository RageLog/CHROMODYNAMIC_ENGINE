// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/ConvexSweepProbe3D.hpp
// Standalone Concrete C++23 Construct Class #151: `cd::scene::ConvexSweepProbe3D`
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

// 151: ShapeCast3D -> ConvexSweepProbe3D
class ConvexSweepProbe3D final
    : public ChromaConstructBase<151, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 3> sweep_vector_m    { 0.0F, -2.0F, 0.0F };
    float                sphere_radius_m   { 0.45F };
    float                capsule_height_m  { 1.10F };
    std::uint32_t        max_contacts      { 8u };
    float                safe_fraction     { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ConvexSweepProbe3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ShapeCast3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:27"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "ConvexSweepProbe3D + GjkConvexSweep";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<ConvexSweepProbe3D>(*this);
    }
};

} // namespace cd::scene
