// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpenXrStereoLens3D.hpp
// Standalone Concrete C++23 Construct Class #154: `cd::scene::OpenXrStereoLens3D`
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

// 154: XRCamera3D -> OpenXrStereoLens3D
class OpenXrStereoLens3D final
    : public ChromaConstructBase<154, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float interpupillary_distance_m { 0.064F };
    float comfort_vignette_strength { 0.35F };
    bool  vk_multiview_single_pass  { true };
    bool  foveated_rendering_active { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpenXrStereoLens3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XRCamera3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "25:08"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpenXrStereoLens3D + VkMultiview";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpenXrStereoLens3D>(*this);
    }
};

} // namespace cd::scene
