// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/TransformRelayCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #152: `cd::scene::TransformRelayCapsule3D`
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

// 152: RemoteTransform3D -> TransformRelayCapsule3D
class TransformRelayCapsule3D final
    : public ChromaConstructBase<152, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    ElementId target_element_id    { kInvalidElementId };
    float     spring_damping_hz    { 12.0F };
    bool      relay_position       { true };
    bool      relay_rotation       { true };
    bool      relay_scale          { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "TransformRelayCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RemoteTransform3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:55"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "TransformRelayCapsule3D + SpringRelay";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<TransformRelayCapsule3D>(*this);
    }
};

} // namespace cd::scene
