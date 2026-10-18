// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/AudioEar3D.hpp
// Standalone Concrete C++23 Construct Class #141: `cd::scene::AudioEar3D`
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

// 141: AudioListener3D -> AudioEar3D
class AudioEar3D final
    : public ChromaConstructBase<141, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float interaural_radius_m { 0.0875F };
    float master_gain_db      { 0.0F };
    bool  kemar_binaural_hrtf { true };
    bool  sync_with_camera    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AudioEar3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AudioListener3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "22:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "AudioEar3D + KemarBinauralHRTF";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<AudioEar3D>(*this);
    }
};

} // namespace cd::scene
