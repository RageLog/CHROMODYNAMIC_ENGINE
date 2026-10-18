// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/SdfShadowOccluder2D.hpp
// Standalone Concrete C++23 Construct Class #47: `cd::scene::SdfShadowOccluder2D`
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

// [47] LightOccluder2D -> SdfShadowOccluder2D (07:23)
class SdfShadowOccluder2D final
    : public ChromaConstructBase<47, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    std::uint32_t light_mask         { 1u };
    bool          cull_clockwise     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SdfShadowOccluder2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "LightOccluder2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "07:23"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "SdfShadowOccluder2D + JumpFloodSDF"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<SdfShadowOccluder2D>(*this); }
};

} // namespace cd::scene
