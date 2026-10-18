// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiKineticScrollBar.hpp
// Standalone Concrete C++23 Construct Class #204: `cd::scene::UiKineticScrollBar`
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

// 204: ScrollBar -> UiKineticScrollBar
class UiKineticScrollBar final
    : public ChromaConstructBase<204, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float scroll_value      { 0.0F };
    float content_extent_px { 600.0F };
    bool  auto_hide         { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiKineticScrollBar"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ScrollBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:17"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiKineticScrollBar + ThumbDrag"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiKineticScrollBar>(*this); }
};

} // namespace cd::scene
