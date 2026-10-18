// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalRule.hpp
// Standalone Concrete C++23 Construct Class #183: `cd::scene::UiHorizontalRule`
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

// 183: HSeparator -> UiHorizontalRule
class UiHorizontalRule final
    : public ChromaConstructBase<183, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float margin_vertical_px { 4.0F };
    bool  pixel_snapped      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalRule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HSeparator"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalRule + PixelSnapped"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalRule>(*this); }
};

} // namespace cd::scene
