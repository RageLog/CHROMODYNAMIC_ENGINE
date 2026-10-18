// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/ConcavePolygonHullCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #113: `cd::scene::ConcavePolygonHullCapsule3D`
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
class ConcavePolygonHullCapsule3D final
    : public ChromaConstructBase<113, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         prism_depth_m       { 1.0F };
    std::uint32_t max_hull_vertices   { 64u };
    bool          convex_decompose_on { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ConcavePolygonHullCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionPolygon3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "16:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "ConcavePolygonHullCapsule3D + Quickhull3D";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<ConcavePolygonHullCapsule3D>(*this);
    }
};

} // namespace cd::scene
