// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarFrustumSleepGate2D.hpp
// Standalone Concrete C++23 Construct Class #68: `cd::scene::PlanarFrustumSleepGate2D`
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

// [68] VisibleOnScreenEnabler2D -> PlanarFrustumSleepGate2D (10:01)
class PlanarFrustumSleepGate2D final
    : public ChromaConstructBase<68, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    bool pause_particles_offscreen { true };
    bool pause_ai_offscreen        { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarFrustumSleepGate2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VisibleOnScreenEnabler2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:01"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarFrustumSleepGate2D + ChunkBitGate"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarFrustumSleepGate2D>(*this); }
};

} // namespace cd::scene
