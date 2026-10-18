// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarLightBase2D.hpp
// Standalone Concrete C++23 Construct Class #44: `cd::scene::PlanarLightBase2D`
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

// [44] Light2D -> PlanarLightBase2D (06:39)
class PlanarLightBase2D final
    : public ChromaConstructBase<44, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float energy            { 1.0F };
    float normal_height_px  { 24.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarLightBase2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Light2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "06:39"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarLightBase2D + NormalSpecular"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarLightBase2D>(*this); }
};

} // namespace cd::scene
