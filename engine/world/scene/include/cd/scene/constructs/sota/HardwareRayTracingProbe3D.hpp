// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/HardwareRayTracingProbe3D.hpp
// Standalone Concrete C++23 Construct Class #219: `cd::scene::HardwareRayTracingProbe3D`
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

// 219: HardwareRayTracingProbe3D
class HardwareRayTracingProbe3D final
    : public ChromaConstructBase<219, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    float         max_ray_distance_m   { 120.0F };
    std::uint32_t max_bounces          { 2u };
    bool          trace_contact_shadow { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "HardwareRayTracingProbe3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HardwareRayTracingProbe3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "HardwareRayTracingProbe3D + VK_KHR_ray_query + MirrorSphere"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<HardwareRayTracingProbe3D>(*this); }
};

} // namespace cd::scene
