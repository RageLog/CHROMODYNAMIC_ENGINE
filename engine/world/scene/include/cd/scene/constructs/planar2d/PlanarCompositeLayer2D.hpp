// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarCompositeLayer2D.hpp
// Standalone Concrete C++23 Construct Class #69: `cd::scene::PlanarCompositeLayer2D`
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

// [69] CanvasGroup -> PlanarCompositeLayer2D (10:17)
class PlanarCompositeLayer2D final
    : public ChromaConstructBase<69, ConstructCategory::Planar2D, ConstructRole::Stage>
{
public:
    float group_opacity   { 1.0F };
    float fit_margin_px   { 10.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarCompositeLayer2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CanvasGroup"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:17"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarCompositeLayer2D + OffscreenComposite"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarCompositeLayer2D>(*this); }
};

} // namespace cd::scene
