// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiCommandButton.hpp
// Standalone Concrete C++23 Construct Class #188: `cd::scene::UiCommandButton`
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

// 188: Button -> UiCommandButton
class UiCommandButton final
    : public ChromaConstructBase<188, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string label_text     { "Execute" };
    std::string shortcut_badge { "Ctrl+E" };
    bool        clip_label     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiCommandButton"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Button"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiCommandButton + VectorIcon + Clip"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiCommandButton>(*this); }
};

} // namespace cd::scene
