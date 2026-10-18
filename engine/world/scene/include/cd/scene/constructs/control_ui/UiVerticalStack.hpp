// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVerticalStack.hpp
// Standalone Concrete C++23 Construct Class #163: `cd::scene::UiVerticalStack`
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

// 163: VBoxContainer -> UiVerticalStack
class UiVerticalStack final
    : public ChromaConstructBase<163, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float spacing_px    { 6.0F };
    bool  pin_header    { false };
    bool  pin_footer    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVerticalStack"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VBoxContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:59"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVerticalStack + PinnedHeaderScroll"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVerticalStack>(*this); }
};

} // namespace cd::scene
