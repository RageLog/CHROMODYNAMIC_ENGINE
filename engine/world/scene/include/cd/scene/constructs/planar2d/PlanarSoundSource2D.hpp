// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSoundSource2D.hpp
// Standalone Concrete C++23 Construct Class #39: `cd::scene::PlanarSoundSource2D`
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

// [39] AudioStreamPlayer2D -> PlanarSoundSource2D (05:39)
class PlanarSoundSource2D final
    : public ChromaConstructBase<39, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float max_distance_px   { 2000.0F };
    bool  occlusion_filter  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSoundSource2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AudioStreamPlayer2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "05:39"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSoundSource2D + OcclusionLPF"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSoundSource2D>(*this); }
};

} // namespace cd::scene
