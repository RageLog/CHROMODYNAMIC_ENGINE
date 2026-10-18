// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiValueSlider.hpp
// Standalone Concrete C++23 Construct Class #207: `cd::scene::UiValueSlider`
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

// 207: Slider -> UiValueSlider
class UiValueSlider final
    : public ChromaConstructBase<207, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float       value            { 0.5F };
    float       min_value        { 0.0F };
    float       max_value        { 1.0F };
    bool        rotary_knob_mode { false };
    std::string format_spec      { "%.2f" };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiValueSlider"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Slider"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:33"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiValueSlider + KnobFloat + InlineEdit"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiValueSlider>(*this); }
};

} // namespace cd::scene
