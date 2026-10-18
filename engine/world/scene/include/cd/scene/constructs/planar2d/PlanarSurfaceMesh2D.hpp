// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarSurfaceMesh2D.hpp
// Standalone Concrete C++23 Construct Class #50: `cd::scene::PlanarSurfaceMesh2D`
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

// [50] MeshInstance2D -> PlanarSurfaceMesh2D (07:56)
class PlanarSurfaceMesh2D final
    : public ChromaConstructBase<50, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t triangle_count { 64u };
    bool          pbr_shading    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarSurfaceMesh2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MeshInstance2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "07:56"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarSurfaceMesh2D + IndexedMesh"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarSurfaceMesh2D>(*this); }
};

} // namespace cd::scene
