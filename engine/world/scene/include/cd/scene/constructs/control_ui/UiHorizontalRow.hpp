// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiHorizontalRow.hpp
// Standalone Concrete C++23 Construct Class #164: `cd::scene::UiHorizontalRow`
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

// 164: HBoxContainer -> UiHorizontalRow
class UiHorizontalRow final
    : public ChromaConstructBase<164, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float spacing_px        { 6.0F };
    bool  collapse_overflow { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiHorizontalRow"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HBoxContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:59"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiHorizontalRow + FlexStretch"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiHorizontalRow>(*this); }
};

} // namespace cd::scene
