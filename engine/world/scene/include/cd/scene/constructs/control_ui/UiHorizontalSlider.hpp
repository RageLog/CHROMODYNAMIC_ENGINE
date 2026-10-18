// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalSlider.hpp
// Standalone Concrete C++23 Construct Class #208: `cd::scene::UiHorizontalSlider`
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

// 208: HSlider -> UiHorizontalSlider
class UiHorizontalSlider final
    : public ChromaConstructBase<208, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float value            { 0.5F };
    bool  draw_accent_fill { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalSlider"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HSlider"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:33"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalSlider + AccentTrack"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalSlider>(*this); }
};

} // namespace cd::scene
