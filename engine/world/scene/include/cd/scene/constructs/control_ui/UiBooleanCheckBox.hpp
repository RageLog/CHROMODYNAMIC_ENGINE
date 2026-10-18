// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiBooleanCheckBox.hpp
// Standalone Concrete C++23 Construct Class #191: `cd::scene::UiBooleanCheckBox`
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

// 191: CheckBox -> UiBooleanCheckBox
class UiBooleanCheckBox final
    : public ChromaConstructBase<191, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    bool        checked         { true };
    std::string label_text      { "Enabled" };
    bool        record_undo_cmd { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiBooleanCheckBox"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CheckBox"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:56"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiBooleanCheckBox + VectorCheck"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiBooleanCheckBox>(*this); }
};

} // namespace cd::scene
