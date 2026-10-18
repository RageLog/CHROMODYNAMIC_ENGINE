// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/GpuRibbonStroke2D.hpp
// Standalone Concrete C++23 Construct Class #48: `cd::scene::GpuRibbonStroke2D`
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

// [48] Line2D -> GpuRibbonStroke2D (07:33)
class GpuRibbonStroke2D final
    : public ChromaConstructBase<48, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float stroke_width_px { 4.0F };
    bool  antialiased     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "GpuRibbonStroke2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Line2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "07:33"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "GpuRibbonStroke2D + AntiAliasedBezier"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<GpuRibbonStroke2D>(*this); }
};

} // namespace cd::scene
