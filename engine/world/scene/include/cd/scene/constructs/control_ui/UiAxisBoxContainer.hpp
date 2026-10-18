// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiAxisBoxContainer.hpp
// Standalone Concrete C++23 Construct Class #162: `cd::scene::UiAxisBoxContainer`
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

// 162: BoxContainer -> UiAxisBoxContainer
class UiAxisBoxContainer final
    : public ChromaConstructBase<162, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool  vertical_axis   { false };
    float item_gap_px     { 8.0F };
    bool  animate_reorder { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiAxisBoxContainer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "BoxContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:59"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiAxisBoxContainer + FlexGap"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiAxisBoxContainer>(*this); }
};

} // namespace cd::scene
