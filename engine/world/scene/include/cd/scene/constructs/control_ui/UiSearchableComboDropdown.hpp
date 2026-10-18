// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiSearchableComboDropdown.hpp
// Standalone Concrete C++23 Construct Class #194: `cd::scene::UiSearchableComboDropdown`
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

// 194: OptionButton -> UiSearchableComboDropdown
class UiSearchableComboDropdown final
    : public ChromaConstructBase<194, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::int32_t selected_index   { 0 };
    std::string  search_filter    { "" };
    bool         enable_search    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiSearchableComboDropdown"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "OptionButton"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "30:11"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiSearchableComboDropdown + BeginCombo"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiSearchableComboDropdown>(*this); }
};

} // namespace cd::scene
