// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/DeterministicPulseTimer.hpp
// Standalone Concrete C++23 Construct Class #20: `cd::scene::DeterministicPulseTimer`
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

// [20] Timer -> DeterministicPulseTimer (41:00)
class DeterministicPulseTimer final
    : public ChromaConstructBase<20, ConstructCategory::CoreSystem, ConstructRole::PulseBus>
{
public:
    float         wait_time_sec { 1.0F };
    bool          one_shot      { false };
    std::uint32_t fired_count   { 0u };

    void on_tick(Element& host, float dt) noexcept override
    {
        IChromaConstruct::on_tick(host, dt);
        if (enabled_ && wait_time_sec > 0.0F && elapsed_time_ >= wait_time_sec)
        {
            ++fired_count;
            if (one_shot)
            {
                enabled_ = false;
            }
            else
            {
                elapsed_time_ -= wait_time_sec;
            }
        }
    }

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "DeterministicPulseTimer"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Timer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "41:00"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "DeterministicPulseTimer + FixedTickAccum"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<DeterministicPulseTimer>(*this); }
};

} // namespace cd::scene
