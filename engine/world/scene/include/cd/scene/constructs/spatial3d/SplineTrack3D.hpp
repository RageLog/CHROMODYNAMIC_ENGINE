// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SplineTrack3D.hpp
// Standalone Concrete C++23 Construct Class #148: `cd::scene::SplineTrack3D`
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

// 148: Path3D -> SplineTrack3D
class SplineTrack3D final
    : public ChromaConstructBase<148, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         total_arc_length_m     { 24.0F };
    float         catmull_rom_tension    { 0.5F };
    float         default_bank_roll_deg  { 0.0F };
    std::uint32_t arc_lut_subdivisions   { 128u };
    bool          closed_loop            { true };
    bool          bishop_parallel_frame  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SplineTrack3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Path3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:03"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SplineTrack3D + BishopFrameSpline";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SplineTrack3D>(*this);
    }
};

} // namespace cd::scene
