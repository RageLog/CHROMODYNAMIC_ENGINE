// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/TimelinePulseSequencer.hpp
// Standalone Concrete C++23 Construct Class #13: `cd::scene::TimelinePulseSequencer`
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

// [13] AnimationPlayer -> TimelinePulseSequencer (39:27)
class TimelinePulseSequencer final
    : public ChromaConstructBase<13, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    float playback_speed   { 1.0F };
    bool  loop_clip        { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "TimelinePulseSequencer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimationPlayer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:27"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "TimelinePulseSequencer + KinematicMover"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<TimelinePulseSequencer>(*this); }
};

} // namespace cd::scene
