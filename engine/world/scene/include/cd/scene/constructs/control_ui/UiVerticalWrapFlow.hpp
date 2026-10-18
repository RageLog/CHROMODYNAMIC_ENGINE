// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVerticalWrapFlow.hpp
// Standalone Concrete C++23 Construct Class #167: `cd::scene::UiVerticalWrapFlow`
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

// 167: VFlowContainer -> UiVerticalWrapFlow
class UiVerticalWrapFlow final
    : public ChromaConstructBase<167, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float column_gap_px       { 8.0F };
    float row_gap_px          { 6.0F };
    bool  sync_column_widths  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVerticalWrapFlow"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VFlowContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:21"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVerticalWrapFlow + ColumnWrap"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVerticalWrapFlow>(*this); }
};

} // namespace cd::scene
