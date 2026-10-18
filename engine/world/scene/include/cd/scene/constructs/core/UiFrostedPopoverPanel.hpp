// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/UiFrostedPopoverPanel.hpp
// Standalone Concrete C++23 Construct Class #9: `cd::scene::UiFrostedPopoverPanel`
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

// [9] PopupPanel -> UiFrostedPopoverPanel (38:36)
class UiFrostedPopoverPanel final
    : public ChromaConstructBase<9, ConstructCategory::CoreSystem, ConstructRole::Element>
{
public:
    float elevation_shadow_px { 16.0F };
    float corner_radius_px    { 8.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiFrostedPopoverPanel"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PopupPanel"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "38:36"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiFrostedPopoverPanel + ElevationShadow"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiFrostedPopoverPanel>(*this); }
};

} // namespace cd::scene
