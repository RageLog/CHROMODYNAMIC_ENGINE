// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVerticalScrollBar.hpp
// Standalone Concrete C++23 Construct Class #206: `cd::scene::UiVerticalScrollBar`
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

// 206: VScrollBar -> UiVerticalScrollBar
class UiVerticalScrollBar final
    : public ChromaConstructBase<206, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float scroll_y_px          { 0.0F };
    float max_scroll_px        { 2048.0F };
    bool  popup_occlusion_safe { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVerticalScrollBar"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VScrollBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:17"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVerticalScrollBar + OcclusionGuard"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVerticalScrollBar>(*this); }
};

} // namespace cd::scene
