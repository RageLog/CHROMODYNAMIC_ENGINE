// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/RollbackNetcodeDeterminism.hpp
// Standalone Concrete C++23 Construct Class #237: `cd::scene::RollbackNetcodeDeterminism`
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

// 237: RollbackNetcodeDeterminism
class RollbackNetcodeDeterminism final
    : public ChromaConstructBase<237, ConstructCategory::NextGenSOTA, ConstructRole::PulseBus>
{
public:
    std::uint32_t max_rollback_frames { 8u };
    std::uint32_t input_delay_frames  { 2u };
    bool          verify_checksums    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RollbackNetcodeDeterminism"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RollbackNetcodeDeterminism"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "RollbackNetcodeDeterminism + SnapshotRing + Resimulate"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<RollbackNetcodeDeterminism>(*this); }
};

} // namespace cd::scene
