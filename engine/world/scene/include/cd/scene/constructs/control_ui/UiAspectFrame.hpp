// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiAspectFrame.hpp
// Standalone Concrete C++23 Construct Class #161: `cd::scene::UiAspectFrame`
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

// 161: AspectRatioContainer -> UiAspectFrame
class UiAspectFrame final
    : public ChromaConstructBase<161, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    float        aspect_ratio { 16.0F / 9.0F };
    std::uint8_t stretch_mode { 0u };
    bool         subpixel_fit { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiAspectFrame"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AspectRatioContainer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:44"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiAspectFrame + AspectLock"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiAspectFrame>(*this); }
};

} // namespace cd::scene
