// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/RadialLight2DElement.hpp
// Standalone Concrete C++23 Construct Class #45: `cd::scene::RadialLight2DElement`
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

// [45] PointLight2D -> RadialLight2DElement (06:46)
class RadialLight2DElement final
    : public ChromaConstructBase<45, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float radius_px        { 256.0F };
    bool  sdf_soft_shadows { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RadialLight2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PointLight2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "06:46"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "RadialLight2DElement + SdfSoftShadow"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<RadialLight2DElement>(*this); }
};

} // namespace cd::scene
