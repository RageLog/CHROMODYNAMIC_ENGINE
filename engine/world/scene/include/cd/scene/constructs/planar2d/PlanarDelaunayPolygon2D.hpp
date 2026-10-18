// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarDelaunayPolygon2D.hpp
// Standalone Concrete C++23 Construct Class #60: `cd::scene::PlanarDelaunayPolygon2D`
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

// [60] Polygon2D -> PlanarDelaunayPolygon2D (08:58)
class PlanarDelaunayPolygon2D final
    : public ChromaConstructBase<60, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t vertex_count { 8u };
    bool          invert_border { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarDelaunayPolygon2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Polygon2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:58"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarDelaunayPolygon2D + DelaunayMesh"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarDelaunayPolygon2D>(*this); }
};

} // namespace cd::scene
