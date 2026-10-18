// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmMesh3D.hpp
// Standalone Concrete C++23 Construct Class #76: `cd::scene::SwarmMesh3D`
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
class SwarmMesh3D final
    : public ChromaConstructBase<76, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t instance_count       { 1024u };
    float         scatter_radius_m     { 25.0F };
    bool          gpu_frustum_cull     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmMesh3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MultiMeshInstance3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "11:58"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmMesh3D + IndirectInstancing";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmMesh3D>(*this);
    }
};

} // namespace cd::scene
