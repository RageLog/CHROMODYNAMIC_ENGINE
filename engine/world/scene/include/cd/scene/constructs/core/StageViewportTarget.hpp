// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/StageViewportTarget.hpp
// Standalone Concrete C++23 Construct Class #1: `cd::scene::StageViewportTarget`
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

// [1] Viewport -> StageViewportTarget (37:08)
class StageViewportTarget final
    : public ChromaConstructBase<1, ConstructCategory::CoreSystem, ConstructRole::Stage>
{
public:
    std::uint32_t width_px    { 1920u };
    std::uint32_t height_px   { 1080u };
    bool          hdr_tonemap { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "StageViewportTarget"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Viewport"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "37:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "StageViewportTarget + TonemapCameraModifier"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<StageViewportTarget>(*this); }
};

} // namespace cd::scene
