// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiRadialProgressRing.hpp
// Standalone Concrete C++23 Construct Class #202: `cd::scene::UiRadialProgressRing`
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

// 202: TextureProgressBar -> UiRadialProgressRing
class UiRadialProgressRing final
    : public ChromaConstructBase<202, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float progress_01     { 0.75F };
    float radius_px       { 24.0F };
    float ring_thickness  { 4.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiRadialProgressRing"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TextureProgressBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:01"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiRadialProgressRing + CooldownArc"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiRadialProgressRing>(*this); }
};

} // namespace cd::scene
