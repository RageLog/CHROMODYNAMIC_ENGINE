// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiDividerRule.hpp
// Standalone Concrete C++23 Construct Class #182: `cd::scene::UiDividerRule`
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

// 182: Separator -> UiDividerRule
class UiDividerRule final
    : public ChromaConstructBase<182, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string section_title { "" };
    float       thickness_px  { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiDividerRule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Separator"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:41"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiDividerRule + SeparatorText"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiDividerRule>(*this); }
};

} // namespace cd::scene
