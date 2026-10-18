// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGaugeBar.hpp
// Standalone Concrete C++23 Construct Class #201: `cd::scene::UiGaugeBar`
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

// 201: ProgressBar -> UiGaugeBar
class UiGaugeBar final
    : public ChromaConstructBase<201, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float fraction         { 0.85F };
    float shield_fraction  { 0.50F };
    bool  show_percentage  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGaugeBar"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ProgressBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:01"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGaugeBar + HealthShieldHUD"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGaugeBar>(*this); }
};

} // namespace cd::scene
