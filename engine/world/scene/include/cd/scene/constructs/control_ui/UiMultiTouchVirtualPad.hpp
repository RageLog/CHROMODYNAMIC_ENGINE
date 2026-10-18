// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiMultiTouchVirtualPad.hpp
// Standalone Concrete C++23 Construct Class #218: `cd::scene::UiMultiTouchVirtualPad`
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

// 218: TouchScreenButton -> UiMultiTouchVirtualPad
class UiMultiTouchVirtualPad final
    : public ChromaConstructBase<218, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 2> stick_axis      { 0.0F, 0.0F };
    float                deadzone_radius { 0.15F };
    bool                 passby_press    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiMultiTouchVirtualPad"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TouchScreenButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:43"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiMultiTouchVirtualPad + VirtualThumbstick"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiMultiTouchVirtualPad>(*this); }
};

} // namespace cd::scene
