// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiDockTabWorkspace.hpp
// Standalone Concrete C++23 Construct Class #176: `cd::scene::UiDockTabWorkspace`
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

// 176: TabContainer -> UiDockTabWorkspace
class UiDockTabWorkspace final
    : public ChromaConstructBase<176, ConstructCategory::ControlUI, ConstructRole::Stage>
{
public:
    std::uint16_t active_tab_index { 0u };
    bool          allow_undocking  { true };
    bool          tabs_visible     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiDockTabWorkspace"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TabContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "27:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiDockTabWorkspace + MultiTabDock"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiDockTabWorkspace>(*this); }
};

} // namespace cd::scene
