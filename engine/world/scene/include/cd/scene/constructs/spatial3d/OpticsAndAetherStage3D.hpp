// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OpticsAndAetherStage3D.hpp
// Standalone Concrete C++23 Construct Class #83: `cd::scene::OpticsAndAetherStage3D`
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

// -----------------------------------------------------------------------------
class OpticsAndAetherStage3D final
    : public ChromaConstructBase<83, ConstructCategory::Spatial3D, ConstructRole::Stage>
{
public:
    float exposure_ev100     { 1.0F };
    float bloom_intensity    { 0.08F };
    bool  enable_bruneton_sky { true };
    bool  enable_agx_tonemap  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OpticsAndAetherStage3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "WorldEnvironment"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:01"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OpticsAndAetherStage3D + Sky + Bloom + AgX";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OpticsAndAetherStage3D>(*this);
    }
};

} // namespace cd::scene
