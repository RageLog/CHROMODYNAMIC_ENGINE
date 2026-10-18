// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/NetAuthorityVesselSpawner.hpp
// Standalone Concrete C++23 Construct Class #16: `cd::scene::NetAuthorityVesselSpawner`
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

// [16] MultiplayerSpawner -> NetAuthorityVesselSpawner (39:55)
class NetAuthorityVesselSpawner final
    : public ChromaConstructBase<16, ConstructCategory::CoreSystem, ConstructRole::Stage>
{
public:
    float         interest_radius_m { 120.0F };
    std::uint32_t spawn_limit       { 256u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NetAuthorityVesselSpawner"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MultiplayerSpawner"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:55"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "NetAuthorityVesselSpawner + SpatialInterest"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<NetAuthorityVesselSpawner>(*this); }
};

} // namespace cd::scene
