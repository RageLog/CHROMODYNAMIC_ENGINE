// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/Planar2DElement.hpp
// Standalone Concrete C++23 Construct Class #21: `cd::scene::Planar2DElement`
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

// [21] Node2D -> Planar2DElement (02:29)
class Planar2DElement final
    : public ChromaConstructBase<21, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::int32_t z_index          { 0 };
    bool         z_as_relative    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "Planar2DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Node2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "02:29"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "Planar2DElement + SoATransform2D"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<Planar2DElement>(*this); }
};

} // namespace cd::scene
