// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/VoronoiFractureDestruction3D.hpp
// Standalone Concrete C++23 Construct Class #232: `cd::scene::VoronoiFractureDestruction3D`
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

// 232: VoronoiFractureDestruction3D
class VoronoiFractureDestruction3D final
    : public ChromaConstructBase<232, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    std::uint32_t voronoi_site_count      { 24u };
    float         fracture_impulse_thresh { 18.0F };
    float         debris_lifetime_sec     { 12.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "VoronoiFractureDestruction3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VoronoiFractureDestruction3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "VoronoiFractureDestruction3D + VoronoiCells + BrickPBR"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<VoronoiFractureDestruction3D>(*this); }
};

} // namespace cd::scene
