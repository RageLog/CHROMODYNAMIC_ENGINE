// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarLens2D.hpp
// Standalone Concrete C++23 Construct Class #22: `cd::scene::PlanarLens2D`
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

// [22] Camera2D -> PlanarLens2D (02:39)
class PlanarLens2D final
    : public ChromaConstructBase<22, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    float zoom                { 1.0F };
    float trauma_decay        { 0.8F };
    bool  pixel_perfect_snap  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarLens2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Camera2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "02:39"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarLens2D + SpringDeadzone"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarLens2D>(*this); }
};

} // namespace cd::scene
