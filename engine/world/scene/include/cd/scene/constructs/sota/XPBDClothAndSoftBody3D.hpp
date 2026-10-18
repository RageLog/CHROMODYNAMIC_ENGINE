// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/XPBDClothAndSoftBody3D.hpp
// Standalone Concrete C++23 Construct Class #224: `cd::scene::XPBDClothAndSoftBody3D`
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

// 224: XPBDClothAndSoftBody3D
class XPBDClothAndSoftBody3D final
    : public ChromaConstructBase<224, ConstructCategory::NextGenSOTA, ConstructRole::Vessel>
{
public:
    std::uint32_t substep_count        { 8u };
    float         distance_compliance  { 0.0001F };
    float         bending_compliance   { 0.01F };
    float         volume_compliance    { 0.0005F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "XPBDClothAndSoftBody3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "XPBDClothAndSoftBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "XPBDClothAndSoftBody3D + SubsteppedXPBD + SoapFilm520nm"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<XPBDClothAndSoftBody3D>(*this); }
};

} // namespace cd::scene
