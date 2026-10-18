// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiInlineDragSpinBox.hpp
// Standalone Concrete C++23 Construct Class #203: `cd::scene::UiInlineDragSpinBox`
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

// 203: SpinBox -> UiInlineDragSpinBox
class UiInlineDragSpinBox final
    : public ChromaConstructBase<203, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 3> xyz_values  { 0.0F, 0.0F, 0.0F };
    float                drag_speed  { 0.05F };
    bool                 color_coded { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiInlineDragSpinBox"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SpinBox"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiInlineDragSpinBox + XYZColorStrips"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiInlineDragSpinBox>(*this); }
};

} // namespace cd::scene
