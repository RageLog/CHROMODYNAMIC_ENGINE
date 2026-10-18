// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/XpbdConstraintBase3D.hpp
// Standalone Concrete C++23 Construct Class #123: `cd::scene::XpbdConstraintBase3D`
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

// 123: Joint3D -> XpbdConstraintBase3D
class XpbdConstraintBase3D final
    : public ChromaConstructBase<123, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         compliance_alpha          { 0.0001F };
    float         damping_beta              { 0.05F };
    float         break_impulse_threshold   { 10000.0F };
    std::uint32_t solver_substeps           { 8u };
    bool          collide_connected_bodies  { false };
    bool          broken                    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "XpbdConstraintBase3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Joint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:02"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "XpbdConstraintBase3D + 6DofXPBD";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<XpbdConstraintBase3D>(*this);
    }
};

} // namespace cd::scene
