// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalSplitter.hpp
// Standalone Concrete C++23 Construct Class #172: `cd::scene::UiHorizontalSplitter`
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

// 172: HSplitContainer -> UiHorizontalSplitter
class UiHorizontalSplitter final
    : public ChromaConstructBase<172, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float left_right_ratio { 0.25F };
    float sash_thickness   { 4.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalSplitter"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HSplitContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:49"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalSplitter + LeftRightSplit"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalSplitter>(*this); }
};

} // namespace cd::scene
