// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/NestedBooleanAssembly3D.hpp
// Standalone Concrete C++23 Construct Class #108: `cd::scene::NestedBooleanAssembly3D`
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
class NestedBooleanAssembly3D final
    : public ChromaConstructBase<108, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float weld_epsilon_m        { 0.0005F };
    bool  auto_bake_static_blas { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NestedBooleanAssembly3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CSGCombiner3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:14"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "NestedBooleanAssembly3D + CsgDagCombiner";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<NestedBooleanAssembly3D>(*this);
    }
};

} // namespace cd::scene
