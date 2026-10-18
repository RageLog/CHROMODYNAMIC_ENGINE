// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarTransformLinkCapsule2D.hpp
// Standalone Concrete C++23 Construct Class #63: `cd::scene::PlanarTransformLinkCapsule2D`
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

// [63] RemoteTransform2D -> PlanarTransformLinkCapsule2D (09:28)
class PlanarTransformLinkCapsule2D final
    : public ChromaConstructBase<63, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    bool update_position { true };
    bool update_rotation { true };
    bool update_scale    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarTransformLinkCapsule2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RemoteTransform2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "09:28"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarTransformLinkCapsule2D + ZeroLagSync"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarTransformLinkCapsule2D>(*this); }
};

} // namespace cd::scene
