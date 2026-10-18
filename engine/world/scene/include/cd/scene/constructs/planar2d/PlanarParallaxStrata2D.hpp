// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarParallaxStrata2D.hpp
// Standalone Concrete C++23 Construct Class #57: `cd::scene::PlanarParallaxStrata2D`
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

// [57] ParallaxLayer -> PlanarParallaxStrata2D (08:35)
class PlanarParallaxStrata2D final
    : public ChromaConstructBase<57, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float motion_scale_x   { 0.5F };
    float motion_mirror_x  { 1024.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarParallaxStrata2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ParallaxLayer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:35"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarParallaxStrata2D + InfiniteWrap"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarParallaxStrata2D>(*this); }
};

} // namespace cd::scene
