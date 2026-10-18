// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiAnimatedTogglePill.hpp
// Standalone Concrete C++23 Construct Class #192: `cd::scene::UiAnimatedTogglePill`
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

// 192: CheckButton -> UiAnimatedTogglePill
class UiAnimatedTogglePill final
    : public ChromaConstructBase<192, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool  toggled_on       { true };
    float thumb_progress   { 1.0F };
    float spring_stiffness { 18.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiAnimatedTogglePill"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CheckButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:56"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiAnimatedTogglePill + ToggleSwitch"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiAnimatedTogglePill>(*this); }
};

} // namespace cd::scene
