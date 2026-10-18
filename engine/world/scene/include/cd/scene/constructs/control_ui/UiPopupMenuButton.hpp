// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiPopupMenuButton.hpp
// Standalone Concrete C++23 Construct Class #193: `cd::scene::UiPopupMenuButton`
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

// 193: MenuButton -> UiPopupMenuButton
class UiPopupMenuButton final
    : public ChromaConstructBase<193, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string menu_title           { "Actions" };
    bool        clamp_to_screen_edge { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiPopupMenuButton"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MenuButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:04"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiPopupMenuButton + ScreenClampedPopup"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiPopupMenuButton>(*this); }
};

} // namespace cd::scene
