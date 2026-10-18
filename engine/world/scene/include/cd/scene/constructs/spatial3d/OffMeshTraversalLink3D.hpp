// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/OffMeshTraversalLink3D.hpp
// Standalone Concrete C++23 Construct Class #146: `cd::scene::OffMeshTraversalLink3D`
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

// 146: NavigationLink3D -> OffMeshTraversalLink3D
class OffMeshTraversalLink3D final
    : public ChromaConstructBase<146, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> start_endpoint_m   { 0.0F, 0.0F, 0.0F };
    std::array<float, 3> end_endpoint_m     { 0.0F, 3.0F, 4.0F };
    float                jump_arc_apex_m    { 4.5F };
    float                traversal_cost     { 1.25F };
    bool                 bidirectional      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "OffMeshTraversalLink3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationLink3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "23:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "OffMeshTraversalLink3D + JumpPadBoost";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<OffMeshTraversalLink3D>(*this);
    }
};

} // namespace cd::scene
