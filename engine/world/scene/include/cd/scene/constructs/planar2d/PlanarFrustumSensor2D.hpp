// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarFrustumSensor2D.hpp
// Standalone Concrete C++23 Construct Class #67: `cd::scene::PlanarFrustumSensor2D`
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

// [67] VisibleOnScreenNotifier2D -> PlanarFrustumSensor2D (10:01)
class PlanarFrustumSensor2D final
    : public ChromaConstructBase<67, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float cull_margin_px   { 16.0F };
    bool  currently_on_screen { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarFrustumSensor2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VisibleOnScreenNotifier2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:01"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarFrustumSensor2D + SimdCullTest"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarFrustumSensor2D>(*this); }
};

} // namespace cd::scene
