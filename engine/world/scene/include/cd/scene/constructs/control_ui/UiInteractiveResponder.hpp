// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiInteractiveResponder.hpp
// Standalone Concrete C++23 Construct Class #187: `cd::scene::UiInteractiveResponder`
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

// 187: BaseButton -> UiInteractiveResponder
class UiInteractiveResponder final
    : public ChromaConstructBase<187, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool toggle_mode      { false };
    bool pressed_state    { false };
    bool show_focus_ring  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiInteractiveResponder"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "BaseButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiInteractiveResponder + FocusRing"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiInteractiveResponder>(*this); }
};

} // namespace cd::scene
