// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/GPUIndirectCrowd3D.hpp
// Standalone Concrete C++23 Construct Class #229: `cd::scene::GPUIndirectCrowd3D`
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

// 229: GPUIndirectCrowd3D
class GPUIndirectCrowd3D final
    : public ChromaConstructBase<229, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    std::uint32_t instance_count     { 4096u };
    float         avoidance_radius_m { 1.2F };
    bool          use_vat_animation  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "GPUIndirectCrowd3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUIndirectCrowd3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "GPUIndirectCrowd3D + IndirectCount + VatCrowd"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<GPUIndirectCrowd3D>(*this); }
};

} // namespace cd::scene
