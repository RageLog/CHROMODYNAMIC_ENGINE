// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiDragDropTreeOutliner.hpp
// Standalone Concrete C++23 Construct Class #214: `cd::scene::UiDragDropTreeOutliner`
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

// 214: Tree -> UiDragDropTreeOutliner
class UiDragDropTreeOutliner final
    : public ChromaConstructBase<214, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool  show_visibility_lock_toggles { true };
    bool  show_modifier_badges         { true };
    float indent_width_px              { 16.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiDragDropTreeOutliner"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Tree"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:21"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiDragDropTreeOutliner + VisLockBadges"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiDragDropTreeOutliner>(*this); }
};

} // namespace cd::scene
