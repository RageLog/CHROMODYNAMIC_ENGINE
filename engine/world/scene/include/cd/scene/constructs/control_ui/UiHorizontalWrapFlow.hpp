// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalWrapFlow.hpp
// Standalone Concrete C++23 Construct Class #168: `cd::scene::UiHorizontalWrapFlow`
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

// 168: HFlowContainer -> UiHorizontalWrapFlow
class UiHorizontalWrapFlow final
    : public ChromaConstructBase<168, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float         card_min_width_px { 120.0F };
    std::uint16_t max_columns       { 8u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalWrapFlow"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HFlowContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:21"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalWrapFlow + ResponsiveCards"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalWrapFlow>(*this); }
};

} // namespace cd::scene
