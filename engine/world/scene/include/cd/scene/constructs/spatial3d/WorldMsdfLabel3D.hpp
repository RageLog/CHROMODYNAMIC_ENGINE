// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/WorldMsdfLabel3D.hpp
// Standalone Concrete C++23 Construct Class #78: `cd::scene::WorldMsdfLabel3D`
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
class WorldMsdfLabel3D final
    : public ChromaConstructBase<78, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string text                { "CHROMODYNAMIC 3D" };
    float       pixel_size_m        { 0.005F };
    float       msdf_px_range       { 4.0F };
    bool        screen_deoverlap    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "WorldMsdfLabel3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Label3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "12:16"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "WorldMsdfLabel3D + MsdfBillboard";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<WorldMsdfLabel3D>(*this);
    }
};

} // namespace cd::scene
