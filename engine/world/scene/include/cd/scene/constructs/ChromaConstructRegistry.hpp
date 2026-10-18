// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/ChromaConstructRegistry.hpp
// Unified Factory & Registry for All 250 Concrete C++23 Scene Construct Classes
// (219 Godot Video Equivalents + 31 Next-Gen SOTA Beyond-Godot Constructs).
// =============================================================================
#pragma once

#include <cd/scene/constructs/ControlUiAndNextGenSotaConstructs.hpp>
#include <cd/scene/constructs/CoreAndPlanar2DConstructs.hpp>
#include <cd/scene/constructs/IChromaConstruct.hpp>
#include <cd/scene/constructs/Spatial3DVesselPhysicsAndGiConstructs.hpp>
#include <cd/scene/constructs/Spatial3DVisualAndCsgConstructs.hpp>

#include <cctype>
#include <cstdint>
#include <memory>
#include <string_view>

namespace cd::scene
{

inline constexpr std::uint16_t kTotalChromaConstructClasses = 250u;

/// Instantiates the concrete C++23 class (`ChromaElement`, `AvatarVessel3D`,
/// `RoverVessel3D`, `SpringArmCapsule3D`, `HardwareRayTracingProbe3D`, etc.)
/// for any catalog ID in `[0, 249]`.
[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_chroma_construct(
    std::uint16_t id)
{
    if (id <= 70u)
    {
        return instantiate_core_or_planar2d_construct(id);
    }
    if (id <= 114u)
    {
        return instantiate_spatial3d_part_a_construct(id);
    }
    if (id <= 158u)
    {
        return instantiate_spatial3d_part_b_construct(id);
    }
    if (id <= 249u)
    {
        return instantiate_control_ui_or_sota_construct(id);
    }
    return nullptr;
}

namespace detail_construct_lookup
{
    [[nodiscard]] inline bool iequals(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const auto ca = static_cast<unsigned char>(a[i]);
            const auto cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb))
            {
                return false;
            }
        }
        return true;
    }
} // namespace detail_construct_lookup

/// Instantiates the concrete C++23 construct class matching either its native
/// CHROMODYNAMIC class name (e.g. `"AvatarVessel3D"`, `"RoverVessel3D"`,
/// `"SpringArmCapsule3D"`, `"HardwareRayTracingProbe3D"`) or its Godot 4
/// equivalent name (e.g. `"CharacterBody3D"`, `"VehicleBody3D"`, `"SpringArm3D"`).
[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_chroma_construct_by_name(
    std::string_view name)
{
    if (name.empty())
    {
        return nullptr;
    }
    for (std::uint16_t id = 0u; id < kTotalChromaConstructClasses; ++id)
    {
        auto inst = instantiate_chroma_construct(id);
        if (!inst)
        {
            continue;
        }
        if (detail_construct_lookup::iequals(inst->chroma_class_name(), name) ||
            detail_construct_lookup::iequals(inst->godot_equivalent_name(), name))
        {
            return inst;
        }
    }
    return nullptr;
}

/// Spawns a concrete C++23 construct class into an `ElementCapsuleTree`, calls
/// its polymorphic `on_mount(Element&)`, and optionally nests it under `parent_id`.
[[nodiscard]] inline ElementId spawn_chroma_construct_in_tree(
    ElementCapsuleTree& tree,
    std::uint16_t       catalog_id,
    ElementId           parent_id = kInvalidElementId,
    PropagationMask     mask      = PropagationMask::kAll)
{
    auto construct = instantiate_chroma_construct(catalog_id);
    if (!construct)
    {
        return kInvalidElementId;
    }
    const ElementDomain dom = map_category_and_role_to_domain(
        construct->category(), construct->role());
    const ElementId elem_id = tree.create_element(construct->chroma_class_name(), dom);
    if (auto* elem = tree.find(elem_id))
    {
        construct->on_mount(*elem);
    }
    if (parent_id != kInvalidElementId)
    {
        (void)tree.nest_child(parent_id, elem_id, mask);
    }
    return elem_id;
}

} // namespace cd::scene
