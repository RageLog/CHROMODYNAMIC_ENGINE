// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiReorderableTabStrip.hpp
// Standalone Concrete C++23 Construct Class #212: `cd::scene::UiReorderableTabStrip`
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

// 212: TabBar -> UiReorderableTabStrip
class UiReorderableTabStrip final
    : public ChromaConstructBase<212, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::uint16_t active_tab_id    { 0u };
    bool          accent_underline { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiReorderableTabStrip"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TabBar"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:03"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiReorderableTabStrip + AccentUnderline"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiReorderableTabStrip>(*this); }
};

} // namespace cd::scene
