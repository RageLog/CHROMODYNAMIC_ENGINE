// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiAnchorCenterFrame.hpp
// Standalone Concrete C++23 Construct Class #165: `cd::scene::UiAnchorCenterFrame`
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

// 165: CenterContainer -> UiAnchorCenterFrame
class UiAnchorCenterFrame final
    : public ChromaConstructBase<165, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool  use_top_left  { false };
    bool  spring_bounce { true };
    float dpi_scale     { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiAnchorCenterFrame"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CenterContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiAnchorCenterFrame + DpiCenter"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiAnchorCenterFrame>(*this); }
};

} // namespace cd::scene
