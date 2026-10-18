// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarDepthStage2D.hpp
// Standalone Concrete C++23 Construct Class #56: `cd::scene::PlanarDepthStage2D`
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

// [56] ParallaxBackground -> PlanarDepthStage2D (08:35)
class PlanarDepthStage2D final
    : public ChromaConstructBase<56, ConstructCategory::Planar2D, ConstructRole::Stage>
{
public:
    float scroll_base_scale { 1.0F };
    bool  ignore_camera_zoom { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarDepthStage2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ParallaxBackground"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarDepthStage2D + MultiLayerScroll"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarDepthStage2D>(*this); }
};

} // namespace cd::scene
