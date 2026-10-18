// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarBeacon2D.hpp
// Standalone Concrete C++23 Construct Class #49: `cd::scene::PlanarBeacon2D`
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

// [49] Marker2D -> PlanarBeacon2D (07:47)
class PlanarBeacon2D final
    : public ChromaConstructBase<49, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float       gizmo_extents_px { 12.0F };
    std::string waypoint_tag     { "SpawnPoint" };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarBeacon2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Marker2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "07:47"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarBeacon2D + SpawnTarget"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarBeacon2D>(*this); }
};

} // namespace cd::scene
