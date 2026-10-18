// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/core/DspSoundEmitterCapsule.hpp
// Standalone Concrete C++23 Construct Class #12: `cd::scene::DspSoundEmitterCapsule`
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

// [12] AudioStreamPlayer -> DspSoundEmitterCapsule (39:18)
class DspSoundEmitterCapsule final
    : public ChromaConstructBase<12, ConstructCategory::CoreSystem, ConstructRole::TraitCapsule>
{
public:
    float volume_db   { 0.0F };
    float pitch_scale { 1.0F };
    bool  autoplay    { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "DspSoundEmitterCapsule"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AudioStreamPlayer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "39:18"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "DspSoundEmitterCapsule + LockFreePcmMixer"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<DspSoundEmitterCapsule>(*this); }
};

} // namespace cd::scene
