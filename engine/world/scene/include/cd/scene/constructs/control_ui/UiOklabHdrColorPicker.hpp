// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiOklabHdrColorPicker.hpp
// Standalone Concrete C++23 Construct Class #196: `cd::scene::UiOklabHdrColorPicker`
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

// 196: ColorPicker -> UiOklabHdrColorPicker
class UiOklabHdrColorPicker final
    : public ChromaConstructBase<196, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 3> linear_rgb       { 1.0F, 0.92F, 0.80F };
    float                kelvin_cct       { 6500.0F };
    float                hdr_exposure_ev  { 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiOklabHdrColorPicker"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ColorPicker"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:22"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiOklabHdrColorPicker + KelvinAndHex"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiOklabHdrColorPicker>(*this); }
};

} // namespace cd::scene
