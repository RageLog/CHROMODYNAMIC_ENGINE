// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGradientQuad.hpp
// Standalone Concrete C++23 Construct Class #179: `cd::scene::UiGradientQuad`
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

// 179: ColorRect -> UiGradientQuad
class UiGradientQuad final
    : public ChromaConstructBase<179, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 4> color_top_left     { 0.25F, 0.78F, 0.95F, 1.0F };
    std::array<float, 4> color_bottom_right { 0.14F, 0.42F, 0.78F, 1.0F };
    float                corner_radius_px   { 4.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGradientQuad"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ColorRect"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:24"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGradientQuad + LinearRadialGrad"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGradientQuad>(*this); }
};

} // namespace cd::scene
