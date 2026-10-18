// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiFrame.hpp
// Standalone Concrete C++23 Construct Class #159: `cd::scene::UiFrame`
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

// 159: Control -> UiFrame
class UiFrame final
    : public ChromaConstructBase<159, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 4> anchor_rect   { 0.0F, 0.0F, 1.0F, 1.0F };
    std::array<float, 2> min_size_px   { 64.0F, 32.0F };
    bool                 clip_contents { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiFrame"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Control"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:54"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiFrame + FlexAnchorLayout + ClipStack"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiFrame>(*this); }
};

} // namespace cd::scene
