// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiElevationSurfacePanel.hpp
// Standalone Concrete C++23 Construct Class #185: `cd::scene::UiElevationSurfacePanel`
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

// 185: Panel -> UiElevationSurfacePanel
class UiElevationSurfacePanel final
    : public ChromaConstructBase<185, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint8_t theme_preset_index { 0u };
    float        elevation_dp       { 4.0F };
    float        corner_radius_px   { 6.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiElevationSurfacePanel"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Panel"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:51"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiElevationSurfacePanel + 5Themes"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiElevationSurfacePanel>(*this); }
};

} // namespace cd::scene
