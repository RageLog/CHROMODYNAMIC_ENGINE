// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BooleanCustomMesh3D.hpp
// Standalone Concrete C++23 Construct Class #107: `cd::scene::BooleanCustomMesh3D`
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
class BooleanCustomMesh3D final
    : public ChromaConstructBase<107, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::string custom_mesh_uri          { "res://meshes/boolean_cutter.obj" };
    float       winding_number_threshold { 0.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BooleanCustomMesh3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CSGMesh3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:14"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BooleanCustomMesh3D + WindingNumberCSG";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BooleanCustomMesh3D>(*this);
    }
};

} // namespace cd::scene
