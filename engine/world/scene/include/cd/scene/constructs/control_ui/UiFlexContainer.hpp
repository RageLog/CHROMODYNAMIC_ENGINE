// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiFlexContainer.hpp
// Standalone Concrete C++23 Construct Class #160: `cd::scene::UiFlexContainer`
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

// 160: Container -> UiFlexContainer
class UiFlexContainer final
    : public ChromaConstructBase<160, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float        gap_px       { 6.0F };
    std::uint8_t flex_wrap    { 0u };
    std::uint8_t align_items  { 1u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiFlexContainer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Container"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiFlexContainer + O(N)FlexSolver"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiFlexContainer>(*this); }
};

} // namespace cd::scene
