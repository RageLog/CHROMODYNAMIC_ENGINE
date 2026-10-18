// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHdrSwatchButton.hpp
// Standalone Concrete C++23 Construct Class #195: `cd::scene::UiHdrSwatchButton`
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

// 195: ColorPickerButton -> UiHdrSwatchButton
class UiHdrSwatchButton final
    : public ChromaConstructBase<195, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 4> linear_rgba { 0.96F, 0.52F, 0.32F, 1.0F };
    bool                 edit_alpha  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHdrSwatchButton"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ColorPickerButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:22"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHdrSwatchButton + HexSwatchPopup"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHdrSwatchButton>(*this); }
};

} // namespace cd::scene
