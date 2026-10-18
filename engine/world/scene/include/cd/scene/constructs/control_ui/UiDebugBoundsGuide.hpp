// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiDebugBoundsGuide.hpp
// Standalone Concrete C++23 Construct Class #213: `cd::scene::UiDebugBoundsGuide`
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

// 213: ReferenceRect -> UiDebugBoundsGuide
class UiDebugBoundsGuide final
    : public ChromaConstructBase<213, ConstructCategory::ControlUI, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 4> border_rgba { 0.98F, 0.78F, 0.24F, 0.90F };
    float                border_px   { 1.5F };
    bool                 editor_only { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiDebugBoundsGuide"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ReferenceRect"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "32:12"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiDebugBoundsGuide + SafeZoneRect"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiDebugBoundsGuide>(*this); }
};

} // namespace cd::scene
