// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiPaddingInsetFrame.hpp
// Standalone Concrete C++23 Construct Class #173: `cd::scene::UiPaddingInsetFrame`
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

// 173: MarginContainer -> UiPaddingInsetFrame
class UiPaddingInsetFrame final
    : public ChromaConstructBase<173, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 4> padding_lrtb_px { 12.0F, 12.0F, 8.0F, 8.0F };
    bool                 respect_safe_area { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiPaddingInsetFrame"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MarginContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "27:02"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiPaddingInsetFrame + SafeAreaInset"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiPaddingInsetFrame>(*this); }
};

} // namespace cd::scene
