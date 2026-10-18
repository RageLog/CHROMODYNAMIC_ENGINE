// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiFrostedCardContainer.hpp
// Standalone Concrete C++23 Construct Class #174: `cd::scene::UiFrostedCardContainer`
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

// 174: PanelContainer -> UiFrostedCardContainer
class UiFrostedCardContainer final
    : public ChromaConstructBase<174, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float corner_radius_px { 8.0F };
    float backdrop_blur    { 0.65F };
    float border_thickness { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiFrostedCardContainer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PanelContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "27:15"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiFrostedCardContainer + SdfRoundedRect"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiFrostedCardContainer>(*this); }
};

} // namespace cd::scene
