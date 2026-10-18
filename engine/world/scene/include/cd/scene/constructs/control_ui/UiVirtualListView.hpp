// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVirtualListView.hpp
// Standalone Concrete C++23 Construct Class #210: `cd::scene::UiVirtualListView`
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

// 210: ItemList -> UiVirtualListView
class UiVirtualListView final
    : public ChromaConstructBase<210, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::int32_t selected_item { 0 };
    float        row_height_px { 24.0F };
    bool         drag_drop_enabled { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVirtualListView"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ItemList"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVirtualListView + SearchAndDragDrop"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVirtualListView>(*this); }
};

} // namespace cd::scene
