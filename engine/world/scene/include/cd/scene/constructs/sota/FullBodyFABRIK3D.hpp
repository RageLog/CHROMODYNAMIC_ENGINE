// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/FullBodyFABRIK3D.hpp
// Standalone Concrete C++23 Construct Class #231: `cd::scene::FullBodyFABRIK3D`
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

// 231: FullBodyFABRIK3D
class FullBodyFABRIK3D final
    : public ChromaConstructBase<231, ConstructCategory::NextGenSOTA, ConstructRole::TraitCapsule>
{
public:
    std::uint32_t solver_iterations   { 12u };
    float         convergence_tol_m   { 0.001F };
    float         pelvis_weight       { 0.65F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "FullBodyFABRIK3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "FullBodyFABRIK3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "FullBodyFABRIK3D + MultiEffectorConstrainedIK"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<FullBodyFABRIK3D>(*this); }
};

} // namespace cd::scene
