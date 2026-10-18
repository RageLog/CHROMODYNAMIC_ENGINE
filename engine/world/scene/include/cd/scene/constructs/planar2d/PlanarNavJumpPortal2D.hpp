// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarNavJumpPortal2D.hpp
// Standalone Concrete C++23 Construct Class #53: `cd::scene::PlanarNavJumpPortal2D`
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

// [53] NavigationLink2D -> PlanarNavJumpPortal2D (08:15)
class PlanarNavJumpPortal2D final
    : public ChromaConstructBase<53, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    bool  bidirectional { true };
    float jump_arc_height { 48.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarNavJumpPortal2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationLink2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:15"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarNavJumpPortal2D + JumpArc"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarNavJumpPortal2D>(*this); }
};

} // namespace cd::scene
