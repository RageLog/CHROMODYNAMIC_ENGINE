// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/UiFloatingOverlayElement.hpp
// Standalone Concrete C++23 Construct Class #7: `cd::scene::UiFloatingOverlayElement`
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

// [7] Popup -> UiFloatingOverlayElement (38:19)
class UiFloatingOverlayElement final
    : public ChromaConstructBase<7, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    float screen_clamp_margin_px { 6.0F };
    bool  close_on_outside_click { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiFloatingOverlayElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Popup"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:19"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiFloatingOverlayElement + AutoScreenClamp"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiFloatingOverlayElement>(*this); }
};

} // namespace cd::scene
