// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarBezierSpline2D.hpp
// Standalone Concrete C++23 Construct Class #58: `cd::scene::PlanarBezierSpline2D`
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

// [58] Path2D -> PlanarBezierSpline2D (08:47)
class PlanarBezierSpline2D final
    : public ChromaConstructBase<58, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float bake_interval_px { 5.0F };
    bool  closed_loop      { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarBezierSpline2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Path2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:47"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarBezierSpline2D + ArcLengthLUT"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarBezierSpline2D>(*this); }
};

} // namespace cd::scene
