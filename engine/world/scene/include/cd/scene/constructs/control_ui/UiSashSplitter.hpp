// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiSashSplitter.hpp
// Standalone Concrete C++23 Construct Class #170: `cd::scene::UiSashSplitter`
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

// 170: SplitContainer -> UiSashSplitter
class UiSashSplitter final
    : public ChromaConstructBase<170, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float split_ratio          { 0.5F };
    float min_pane_extent_px   { 80.0F };
    bool  double_click_restore { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiSashSplitter"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SplitContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:49"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiSashSplitter + InteractiveSash"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiSashSplitter>(*this); }
};

} // namespace cd::scene
