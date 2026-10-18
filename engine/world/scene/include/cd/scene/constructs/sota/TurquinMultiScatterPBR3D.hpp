// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/TurquinMultiScatterPBR3D.hpp
// Standalone Concrete C++23 Construct Class #221: `cd::scene::TurquinMultiScatterPBR3D`
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

// 221: TurquinMultiScatterPBR3D
class TurquinMultiScatterPBR3D final
    : public ChromaConstructBase<221, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    float multiscatter_compensation { 1.0F };
    float thin_film_thickness_nm    { 440.0F };
    float thin_film_ior             { 1.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "TurquinMultiScatterPBR3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "TurquinMultiScatterPBR3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "TurquinMultiScatterPBR3D + Turquin2019 + BelcourBarla440nm"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<TurquinMultiScatterPBR3D>(*this); }
};

} // namespace cd::scene
