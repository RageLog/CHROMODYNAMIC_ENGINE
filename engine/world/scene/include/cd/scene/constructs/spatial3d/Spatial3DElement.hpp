// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/Spatial3DElement.hpp
// Standalone Concrete C++23 Construct Class #71: `cd::scene::Spatial3DElement`
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
class Spatial3DElement final
    : public ChromaConstructBase<71, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> euler_degrees_pyr  { 0.0F, 0.0F, 0.0F };
    bool                 ground_snap_active { true };
    bool                 uniform_scale_lock { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "Spatial3DElement"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Node3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "10:43"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "Spatial3DElement + EulerGizmo + GroundSnap";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<Spatial3DElement>(*this);
    }
};

} // namespace cd::scene
