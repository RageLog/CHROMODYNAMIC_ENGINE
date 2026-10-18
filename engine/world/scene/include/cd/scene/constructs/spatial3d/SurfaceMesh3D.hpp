// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SurfaceMesh3D.hpp
// Standalone Concrete C++23 Construct Class #75: `cd::scene::SurfaceMesh3D`
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
class SurfaceMesh3D final
    : public ChromaConstructBase<75, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float metallic       { 1.0F };
    float roughness      { 0.16F };
    float multiscatter   { 1.0F };
    float iridescence_nm { 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SurfaceMesh3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MeshInstance3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "11:45"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SurfaceMesh3D + TurquinPBR + BLAS";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SurfaceMesh3D>(*this);
    }
};

} // namespace cd::scene
