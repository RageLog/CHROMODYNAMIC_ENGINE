// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiVerticalSlider.hpp
// Standalone Concrete C++23 Construct Class #209: `cd::scene::UiVerticalSlider`
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

// 209: VSlider -> UiVerticalSlider
class UiVerticalSlider final
    : public ChromaConstructBase<209, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float fader_db    { 0.0F };
    float min_db      { -60.0F };
    float max_db      { 6.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiVerticalSlider"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VSlider"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "31:33"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiVerticalSlider + AudioFader"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiVerticalSlider>(*this); }
};

} // namespace cd::scene
