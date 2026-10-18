// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiWrapFlowLayout.hpp
// Standalone Concrete C++23 Construct Class #166: `cd::scene::UiWrapFlowLayout`
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

// 166: FlowContainer -> UiWrapFlowLayout
class UiWrapFlowLayout final
    : public ChromaConstructBase<166, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float h_separation_px { 6.0F };
    float v_separation_px { 6.0F };
    bool  reverse_fill    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiWrapFlowLayout"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "FlowContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "26:21"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiWrapFlowLayout + TagCloudWrap"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiWrapFlowLayout>(*this); }
};

} // namespace cd::scene
