// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/UtilityGOAPBrainAI.hpp
// Standalone Concrete C++23 Construct Class #234: `cd::scene::UtilityGOAPBrainAI`
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

// 234: UtilityGOAPBrainAI
class UtilityGOAPBrainAI final
    : public ChromaConstructBase<234, ConstructCategory::NextGenSOTA, ConstructRole::Pilot>
{
public:
    std::uint32_t max_astar_plan_depth { 8u };
    float         replan_interval_sec  { 0.25F };
    float         aggression_utility   { 0.70F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UtilityGOAPBrainAI"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "UtilityGOAPBrainAI"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UtilityGOAPBrainAI + GoapPlanner + SynthPilot"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UtilityGOAPBrainAI>(*this); }
};

} // namespace cd::scene
