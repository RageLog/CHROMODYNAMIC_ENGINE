// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/NeuralTemporalUpscaler3D.hpp
// Standalone Concrete C++23 Construct Class #223: `cd::scene::NeuralTemporalUpscaler3D`
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

// 223: NeuralTemporalUpscaler3D
class NeuralTemporalUpscaler3D final
    : public ChromaConstructBase<223, ConstructCategory::NextGenSOTA, ConstructRole::TraitCapsule>
{
public:
    float         render_resolution_scale { 0.67F };
    float         history_blend_factor    { 0.90F };
    std::uint32_t halton_phase_count      { 16u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NeuralTemporalUpscaler3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NeuralTemporalUpscaler3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "NeuralTemporalUpscaler3D + MotionVectorTAAU"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<NeuralTemporalUpscaler3D>(*this); }
};

} // namespace cd::scene
