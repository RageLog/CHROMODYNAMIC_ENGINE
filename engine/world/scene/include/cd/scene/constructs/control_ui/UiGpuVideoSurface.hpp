// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/control_ui/UiGpuVideoSurface.hpp
// Standalone Concrete C++23 Construct Class #181: `cd::scene::UiGpuVideoSurface`
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

// 181: VideoStreamPlayer -> UiGpuVideoSurface
class UiGpuVideoSurface final
    : public ChromaConstructBase<181, ConstructCategory::ControlUI, ConstructRole::Element>
{
public:
    std::string stream_uri     { "cd://video/intro_cutscene.mp4" };
    bool        autoplay       { true };
    bool        loop_playback  { true };
    float       audio_volume   { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "UiGpuVideoSurface"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "VideoStreamPlayer"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "28:36"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "UiGpuVideoSurface + YuvComputeDecode"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<UiGpuVideoSurface>(*this); }
};

} // namespace cd::scene
