// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/BrunetonPlanetarySky3D.hpp
// Standalone Concrete C++23 Construct Class #227: `cd::scene::BrunetonPlanetarySky3D`
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

// 227: BrunetonPlanetarySky3D
class BrunetonPlanetarySky3D final
    : public ChromaConstructBase<227, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    float rayleigh_scale_height_km { 8.0F };
    float mie_scale_height_km      { 1.2F };
    float mie_anisotropy_g         { 0.80F };
    bool  enable_multiscatter_lut  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BrunetonPlanetarySky3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "BrunetonPlanetarySky3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "BrunetonPlanetarySky3D + MultiScatterLUT + AerialPerspective"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<BrunetonPlanetarySky3D>(*this); }
};

} // namespace cd::scene
