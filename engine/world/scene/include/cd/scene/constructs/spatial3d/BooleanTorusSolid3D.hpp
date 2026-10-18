// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BooleanTorusSolid3D.hpp
// Standalone Concrete C++23 Construct Class #105: `cd::scene::BooleanTorusSolid3D`
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

// -----------------------------------------------------------------------------
class BooleanTorusSolid3D final
    : public ChromaConstructBase<105, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         inner_radius_m { 0.45F };
    float         outer_radius_m { 0.75F };
    std::uint32_t ring_segments  { 32u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BooleanTorusSolid3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CSGTorus3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:14"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BooleanTorusSolid3D + PortalRing";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BooleanTorusSolid3D>(*this);
    }
};

} // namespace cd::scene
