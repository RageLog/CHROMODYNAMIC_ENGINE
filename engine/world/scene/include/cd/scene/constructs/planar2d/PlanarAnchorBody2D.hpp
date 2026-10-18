// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarAnchorBody2D.hpp
// Standalone Concrete C++23 Construct Class #29: `cd::scene::PlanarAnchorBody2D`
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

// [29] StaticBody2D -> PlanarAnchorBody2D (03:37)
class PlanarAnchorBody2D final
    : public ChromaConstructBase<29, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float surface_friction { 0.8F };
    float surface_bounce   { 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarAnchorBody2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "StaticBody2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:37"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarAnchorBody2D + StaticCollider"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarAnchorBody2D>(*this); }
};

} // namespace cd::scene
