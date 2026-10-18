// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SpawnBeacon3D.hpp
// Standalone Concrete C++23 Construct Class #72: `cd::scene::SpawnBeacon3D`
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
class SpawnBeacon3D final
    : public ChromaConstructBase<72, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string   beacon_tag        { "SpawnPoint_Alpha" };
    float         gizmo_extents_m   { 0.55F };
    std::uint32_t waypoint_priority { 1u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SpawnBeacon3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Marker3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:59"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SpawnBeacon3D + StageDirectorTarget";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SpawnBeacon3D>(*this);
    }
};

} // namespace cd::scene
