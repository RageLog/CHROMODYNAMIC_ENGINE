// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiNodeCanvas.hpp
// Standalone Concrete C++23 Construct Class #217: `cd::scene::UiNodeCanvas`
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

// 217: GraphEdit -> UiNodeCanvas
class UiNodeCanvas final
    : public ChromaConstructBase<217, ConstructCategory::ControlUI, ConstructRole::Stage>
{
public:
    std::array<float, 2> pan_offset_px    { 0.0F, 0.0F };
    float                zoom_factor      { 1.0F };
    float                grid_step_px     { 24.0F };
    bool                 show_minimap     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiNodeCanvas"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GraphEdit"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:29"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiNodeCanvas + BezierWires + CurveEditor"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiNodeCanvas>(*this); }
};

} // namespace cd::scene
