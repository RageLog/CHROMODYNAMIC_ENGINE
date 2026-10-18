// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiNumericValueModel.hpp
// Standalone Concrete C++23 Construct Class #200: `cd::scene::UiNumericValueModel`
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

// 200: Range -> UiNumericValueModel
class UiNumericValueModel final
    : public ChromaConstructBase<200, ConstructCategory::ControlUI, ConstructRole::TraitCapsule>
{
public:
    float min_value     { 0.0F };
    float max_value     { 100.0F };
    float current_value { 50.0F };
    float step_value    { 0.1F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiNumericValueModel"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Range"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:54"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiNumericValueModel + InlineNumericEdit"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiNumericValueModel>(*this); }
};

} // namespace cd::scene
