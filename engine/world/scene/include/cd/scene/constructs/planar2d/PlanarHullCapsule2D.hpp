// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarHullCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #27: `cd::scene::PlanarHullCapsule2D`
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

// [27] CollisionShape2D -> PlanarHullCapsule2D (03:25)
class PlanarHullCapsule2D final
    : public ChromaConstructBase<27, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float half_extent_x  { 16.0F };
    float half_extent_y  { 16.0F };
    bool  live_wireframe { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarHullCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionShape2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:25"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarHullCapsule2D + LiveWireframe"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarHullCapsule2D>(*this); }
};

} // namespace cd::scene
