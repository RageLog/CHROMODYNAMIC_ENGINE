// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVerticalSplitter.hpp
// Standalone Concrete C++23 Construct Class #171: `cd::scene::UiVerticalSplitter`
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

// 171: VSplitContainer -> UiVerticalSplitter
class UiVerticalSplitter final
    : public ChromaConstructBase<171, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float top_bottom_ratio { 0.65F };
    float sash_thickness   { 4.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVerticalSplitter"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VSplitContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:49"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVerticalSplitter + TopBottomSplit"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVerticalSplitter>(*this); }
};

} // namespace cd::scene
