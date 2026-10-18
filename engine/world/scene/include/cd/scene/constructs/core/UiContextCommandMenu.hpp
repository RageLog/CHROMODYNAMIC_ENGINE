// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/UiContextCommandMenu.hpp
// Standalone Concrete C++23 Construct Class #8: `cd::scene::UiContextCommandMenu`
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

// [8] PopupMenu -> UiContextCommandMenu (38:30)
class UiContextCommandMenu final
    : public ChromaConstructBase<8, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    std::uint32_t max_visible_items { 16u };
    bool          cascade_submenus  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiContextCommandMenu"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PopupMenu"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:30"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiContextCommandMenu + CascadingSubmenus"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiContextCommandMenu>(*this); }
};

} // namespace cd::scene
