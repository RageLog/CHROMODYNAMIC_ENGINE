// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiSdfNineSliceFrame.hpp
// Standalone Concrete C++23 Construct Class #186: `cd::scene::UiSdfNineSliceFrame`
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

// 186: NinePatchRect -> UiSdfNineSliceFrame
class UiSdfNineSliceFrame final
    : public ChromaConstructBase<186, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::array<float, 4> patch_margins_px { 8.0F, 8.0F, 8.0F, 8.0F };
    bool                 draw_center      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiSdfNineSliceFrame"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NinePatchRect"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "29:04"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiSdfNineSliceFrame + Variant3NinePatch"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiSdfNineSliceFrame>(*this); }
};

} // namespace cd::scene
