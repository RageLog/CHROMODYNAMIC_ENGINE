// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalScrollBar.hpp
// Standalone Concrete C++23 Construct Class #205: `cd::scene::UiHorizontalScrollBar`
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

// 205: HScrollBar -> UiHorizontalScrollBar
class UiHorizontalScrollBar final
    : public ChromaConstructBase<205, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float scroll_x_px   { 0.0F };
    float max_scroll_px { 1024.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalScrollBar"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HScrollBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:17"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalScrollBar + PanTrack"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalScrollBar>(*this); }
};

} // namespace cd::scene
