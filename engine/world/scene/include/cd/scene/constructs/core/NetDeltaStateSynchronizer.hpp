// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/NetDeltaStateSynchronizer.hpp
// Standalone Concrete C++23 Construct Class #17: `cd::scene::NetDeltaStateSynchronizer`
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

// [17] MultiplayerSynchronizer -> NetDeltaStateSynchronizer (39:55)
class NetDeltaStateSynchronizer final
    : public ChromaConstructBase<17, ConstructCategory::CoreSystem, ConstructRole::PulseBus>
{
public:
    float snapshot_rate_hz     { 60.0F };
    bool  hermite_interpolation { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NetDeltaStateSynchronizer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MultiplayerSynchronizer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:55"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "NetDeltaStateSynchronizer + SnapshotRing"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<NetDeltaStateSynchronizer>(*this); }
};

} // namespace cd::scene
