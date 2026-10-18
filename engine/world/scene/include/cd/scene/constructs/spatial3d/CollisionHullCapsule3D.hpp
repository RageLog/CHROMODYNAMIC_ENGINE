// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/CollisionHullCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #112: `cd::scene::CollisionHullCapsule3D`
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
class CollisionHullCapsule3D final
    : public ChromaConstructBase<112, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 3> half_extents_m { 0.5F, 0.5F, 0.5F };
    float                contact_margin { 0.01F };
    bool                 show_wireframe { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "CollisionHullCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CollisionShape3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "16:38"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "CollisionHullCapsule3D + ObbNarrowphase";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<CollisionHullCapsule3D>(*this);
    }
};

} // namespace cd::scene
