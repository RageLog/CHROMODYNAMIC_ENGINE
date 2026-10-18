// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiRibbonMenuBar.hpp
// Standalone Concrete C++23 Construct Class #211: `cd::scene::UiRibbonMenuBar`
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

// 211: MenuBar -> UiRibbonMenuBar
class UiRibbonMenuBar final
    : public ChromaConstructBase<211, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float menu_bar_height_px { 28.0F };
    float ribbon_height_px   { 32.0F };
    bool  show_fps_telemetry { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiRibbonMenuBar"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MenuBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:51"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiRibbonMenuBar + QuickRibbon + Telemetry"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiRibbonMenuBar>(*this); }
};

} // namespace cd::scene
