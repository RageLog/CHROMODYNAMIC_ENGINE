// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SoundSource3D.hpp
// Standalone Concrete C++23 Construct Class #142: `cd::scene::SoundSource3D`
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

// 142: AudioStreamPlayer3D -> SoundSource3D
class SoundSource3D final
    : public ChromaConstructBase<142, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float min_distance_m          { 1.5F };
    float max_distance_m          { 45.0F };
    float doppler_tracking_scale  { 1.0F };
    float air_absorption_coeff    { 0.002F };
    float reverb_send_level       { 0.25F };
    bool  raytraced_occlusion     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SoundSource3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AudioStreamPlayer3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "22:59"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SoundSource3D + HrtfSpatializer";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SoundSource3D>(*this);
    }
};

} // namespace cd::scene
