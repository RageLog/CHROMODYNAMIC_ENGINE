// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioTypes.hpp
//
// Canonical shared types for the Source 2 toolframework2-modeled cd_editor_lib
// subsystems (ADR-20261112). Replaces the file-local anonymous types that
// previously trapped all editor subsystems inside a single 6,455-line TU.
// =============================================================================
#pragma once

#include <cd/ecs/Entity.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/ui/ChromaUi.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace cd::studio
{

enum class PrimitiveKind : std::uint8_t
{
    kCube,
    kSphere,
    kCone,
    kCylinder,
    kTorus,
    kTorusKnot,
    kGltf,        ///< CesiumMan / animated character
    kSponza,      ///< Sponza atrium static scene
    kCustomGltf,  ///< User-loaded disk .gltf/.glb/.obj mesh
};

struct SceneEntity
{
    cd::ecs::Entity handle {};
    std::string     name;
    cd::math::Vec3f tint { 1.0F, 1.0F, 1.0F };
    float           metallic          { 0.0F };
    float           roughness         { 0.5F };
    float           multiscatter_comp { 0.0F };
    float           iridescence_nm    { 0.0F };
    PrimitiveKind   kind              { PrimitiveKind::kCube };
    bool            is_pbr            { false };
    bool            use_texture       { false };
    int             custom_texture_id { 0 };
    std::string     custom_texture_path {};
    std::string     custom_mesh_path    {};
    bool            visible           { true };
    bool            locked            { false };
    cd::math::Mat4f prev_model        { cd::math::Mat4f::identity() };
    bool            prev_model_valid  { false };
};

[[nodiscard]] inline PrimitiveKind kind_from_name(std::string_view n) noexcept
{
    if (n == "Sphere")     return PrimitiveKind::kSphere;
    if (n == "Cone")       return PrimitiveKind::kCone;
    if (n == "Cylinder")   return PrimitiveKind::kCylinder;
    if (n == "Torus")      return PrimitiveKind::kTorus;
    if (n == "TorusKnot")  return PrimitiveKind::kTorusKnot;
    if (n == "Gltf")       return PrimitiveKind::kGltf;
    if (n == "Sponza")     return PrimitiveKind::kSponza;
    if (n == "CustomGltf") return PrimitiveKind::kCustomGltf;
    return PrimitiveKind::kCube;
}

enum class SelKind : std::uint8_t
{
    kEntity = 0,
    kLight  = 1,
};

struct LightRow
{
    std::string      name;
    cd::light::Light light;
    bool             enabled { true };
    float            kelvin  { 6500.0F };
};

enum class GizmoMode : std::uint8_t
{
    kTranslate = 0,
    kRotate    = 1,
    kScale     = 2,
};

struct GizmoState
{
    bool            visible                  { true };
    GizmoMode       mode                     { GizmoMode::kTranslate };
    ImVec2          drag_anchor              { 0.0F, 0.0F };
    cd::math::Vec3f drag_world_start         {};
    cd::math::Vec3f drag_plane_hit_start     {};
    cd::math::Vec3f drag_scale_start         { 1.0F, 1.0F, 1.0F };
    cd::math::Quatf drag_rot_start           {};
    cd::math::Vec3f light_drag_dir_start     { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f light_drag_tangent_start { 1.0F,  0.0F, 0.0F };
    float           light_drag_range_start   { 0.0F };
    float           light_drag_area_w_start  { 1.0F };
    float           light_drag_area_h_start  { 1.0F };
    float           drag_initial_offset      { 0.0F };
    bool            drag_use_ray_plane       { false };
    bool            was_hovered              { false };
};

struct PrevCamBasis
{
    cd::math::Vec3f right  { 1.0F, 0.0F,  0.0F };
    cd::math::Vec3f up     { 0.0F, 1.0F,  0.0F };
    cd::math::Vec3f fwd    { 0.0F, 0.0F, -1.0F };
    cd::math::Vec3f pos    { 0.0F, 0.0F,  0.0F };
    float           half_w { 1.0F };
    float           half_h { 1.0F };
    bool            valid  { false };
};

struct SunLight
{
    cd::math::Vec3f dir       { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f col       { 0.0F,  0.0F, 0.0F };
    float           strength  { 0.0F };
    float           ambient_w { 0.0F };
    bool            has_sun   { false };
};

struct HdrSceneFrame
{
    cd::math::Mat4f vp;
    cd::math::Mat4f vp_unjittered;
    float           aspect { 1.0F };
};

}  // namespace cd::studio

// Expose canonical aliases at global scope for legacy Studio* headers.
using cd::studio::GizmoMode;
using cd::studio::GizmoState;
using cd::studio::HdrSceneFrame;
using cd::studio::kind_from_name;
using cd::studio::LightRow;
using cd::studio::PrevCamBasis;
using cd::studio::PrimitiveKind;
using cd::studio::SceneEntity;
using cd::studio::SelKind;
using cd::studio::SunLight;
