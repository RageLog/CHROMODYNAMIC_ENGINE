// =============================================================================
// StudioModifierSystem.hpp -- CHROMODYNAMIC Domestic 3D Engine Studio
//
// Pure Object-Oriented Modifier Architecture + Drag-and-Drop Builder +
// Real 3D RigidBody & Collider Physics Engine (Zero "StudioX" / R-Showcase).
//
// Key Architectural Principles:
//   1. Every effect, material, light, physics body, collider, and controller is
//      its own standalone, instantiable C++23 class deriving from
//      `cd::studio::IStudioModifier`:
//        * class FogModifier
//        * class BloomLensModifier
//        * class TonemapCameraModifier
//        * class SkyAtmosphereModifier
//        * class PbrMaterialModifier
//        * class RigidBodyModifier
//        * class BoxColliderModifier
//        * class SphereColliderModifier
//        * class CapsuleColliderModifier
//        * class LightEmitterModifier
//        * class SkeletalAnimatorModifier
//        * class CharacterControllerModifier
//   2. Drag-and-Drop (Surukle-Birak) Scene & Modifier Construction:
//        * Drag 3D Objects/Meshes from the Toolbox and drop them directly onto
//          the 3D Viewport (raycasted against ground & colliders with live 3D
//          wireframe preview).
//        * Drag any Modifier class from the Toolbox and drop it onto any 3D
//          Entity in the Viewport, any row in the Scene Hierarchy, or the
//          Modifier Stack in the Inspector.
//        * Drag-and-Drop Node3D entities in the Scene Hierarchy to parent or
//          unparent transforms.
//   3. Real 3D Physics & Collision (`cd::physics::IPhysicsWorld`, `Aabb`,
//      `Sphere`, `Capsule`, `ContactManifold`):
//        * Live in-editor physics simulation (`Simulate Physics in Editor`) AND
//          Play-In-Editor (`F5`).
//        * 3D wireframe collider visualization (Green = free, Red/Orange =
//          colliding + Yellow contact point & normal vector markers).
// =============================================================================
#pragma once

#include <cd/asset/json/Json.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Capsule.hpp>
#include <cd/physics/ContactPoint.hpp>
#include <cd/physics/IPhysicsWorld.hpp>
#include <cd/physics/Sphere.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::studio
{

// =============================================================================
// 1. Polymorphic Modifier Base Class (`IStudioModifier`)
// =============================================================================
enum class ModifierCategory : std::uint8_t
{
    kPhysics     = 0,
    kCollision   = 1,
    kMaterial    = 2,
    kLighting    = 3,
    kEnvironment = 4,
    kAnimation   = 5,
    kGameplay    = 6,
};

[[nodiscard]] inline const char* modifier_category_label(ModifierCategory c) noexcept
{
    switch (c)
    {
        case ModifierCategory::kPhysics:     return "Physics";
        case ModifierCategory::kCollision:   return "Collision";
        case ModifierCategory::kMaterial:    return "Material";
        case ModifierCategory::kLighting:    return "Lighting";
        case ModifierCategory::kEnvironment: return "Environment / PostFX";
        case ModifierCategory::kAnimation:   return "Animation";
        case ModifierCategory::kGameplay:    return "Gameplay";
    }
    return "Modifier";
}

class IStudioModifier
{
public:
    IStudioModifier() noexcept = default;
    virtual ~IStudioModifier() = default;

    IStudioModifier(const IStudioModifier&)            = default;
    IStudioModifier& operator=(const IStudioModifier&) = default;
    IStudioModifier(IStudioModifier&&) noexcept        = default;
    IStudioModifier& operator=(IStudioModifier&&) noexcept = default;

    [[nodiscard]] virtual std::string_view class_name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
    [[nodiscard]] virtual ModifierCategory category() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<IStudioModifier> clone() const = 0;

    /// Draw the modifier's custom controls inside the Inspector Modifier Stack.
    virtual void draw_inspector_ui() = 0;

    bool enabled { true };
};

// =============================================================================
// 2. Concrete Instantiable Modifier Classes (Each its own C++23 Class)
// =============================================================================

/// Standalone Fog & Volumetric Scattering Modifier class.
/// When not attached to the scene, fog is completely disabled.
class FogModifier final : public IStudioModifier
{
public:
    float fog_density           { 0.045F };
    bool  volumetric_scattering { true };
    float light_shafts_strength { 0.80F };
    float aerial_perspective    { 0.18F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "FogModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Fog & Volumetric Shafts"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kEnvironment; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<FogModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::Checkbox("Volumetric Wronski Scattering", &volumetric_scattering);
        ImGui::SliderFloat("Fog Density", &fog_density, 0.0F, 0.25F, "%.4f");
        ImGui::SliderFloat("God-Ray Light Shafts", &light_shafts_strength, 0.0F, 1.50F, "%.2f");
        ImGui::SliderFloat("Aerial Perspective", &aerial_perspective, 0.0F, 1.00F, "%.2f");
    }
};

/// Standalone Bloom & Lens Effects Modifier class.
class BloomLensModifier final : public IStudioModifier
{
public:
    float bloom_intensity      { 0.045F };
    float chromatic_aberration { 0.12F };
    float vignette_strength    { 0.30F };
    float film_grain           { 0.06F };
    float dof_strength         { 0.00F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "BloomLensModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Bloom & Lens Optics"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kEnvironment; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<BloomLensModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Bloom Mip Intensity", &bloom_intensity, 0.0F, 0.25F, "%.3f");
        ImGui::SliderFloat("Depth of Field (DoF)", &dof_strength, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Chromatic Aberration", &chromatic_aberration, 0.0F, 0.50F, "%.2f");
        ImGui::SliderFloat("Vignette", &vignette_strength, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Film Grain", &film_grain, 0.0F, 0.40F, "%.2f");
    }
};

/// Standalone Tonemap, Exposure & Temporal AA Modifier class.
class TonemapCameraModifier final : public IStudioModifier
{
public:
    float exposure         { 1.05F };
    float saturation_boost { 1.18F };
    int   tonemap_op       { 2 }; // 0=Narkowicz, 1=Hill ACES, 2=Hable, 3=AgX
    float taa_amount       { 0.85F };
    float motion_blur      { 0.15F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "TonemapCameraModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Tonemap & Exposure"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kEnvironment; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<TonemapCameraModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Exposure", &exposure, 0.10F, 4.00F, "%.2f");
        ImGui::SliderFloat("Saturation", &saturation_boost, 0.00F, 2.50F, "%.2f");
        const char* ops[] = { "0: ACES Narkowicz", "1: ACES Hill", "2: Uncharted2 Hable", "3: AgX Filmic" };
        ImGui::Combo("Tonemap Curve", &tonemap_op, ops, 4);
        ImGui::SliderFloat("Temporal AA (TAA)", &taa_amount, 0.0F, 0.98F, "%.2f");
        ImGui::SliderFloat("Motion Blur", &motion_blur, 0.0F, 1.00F, "%.2f");
    }
};

/// Standalone Sky, Atmosphere, Clouds & GI Modifier class.
class SkyAtmosphereModifier final : public IStudioModifier
{
public:
    float clouds_coverage { 0.20F };
    float ao_strength     { 0.60F };
    float ssr_strength    { 0.50F };
    bool  ddgi_gi_enabled { false };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "SkyAtmosphereModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Sky, Clouds & GI"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kEnvironment; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<SkyAtmosphereModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Volumetric Clouds", &clouds_coverage, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Ambient Occlusion (AO)", &ao_strength, 0.0F, 1.5F, "%.2f");
        ImGui::SliderFloat("Screen-Space Reflections", &ssr_strength, 0.0F, 1.0F, "%.2f");
        ImGui::Checkbox("Ray-Traced DDGI Global Illumination", &ddgi_gi_enabled);
    }
};

/// Standalone Cook-Torrance PBR Material Modifier class for any 3D Entity.
class PbrMaterialModifier final : public IStudioModifier
{
public:
    cd::math::Vec3f base_color  { 0.85F, 0.86F, 0.88F };
    float           metallic    { 0.15F };
    float           roughness   { 0.35F };
    bool            use_texture { false };

    PbrMaterialModifier() noexcept = default;
    PbrMaterialModifier(cd::math::Vec3f col, float m, float r, bool tex = false) noexcept
        : base_color { col }, metallic { m }, roughness { r }, use_texture { tex }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "PbrMaterialModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "PBR Surface Material"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kMaterial; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<PbrMaterialModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::ColorEdit3("Albedo Color", &base_color.x);
        ImGui::SliderFloat("Metallic", &metallic, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Roughness", &roughness, 0.04F, 1.0F, "%.2f");
        ImGui::Checkbox("Bind Albedo Texture Map", &use_texture);
        ImGui::TextDisabled("Quick Surface Presets:");
        if (ImGui::SmallButton("Chrome"))    { base_color = { 0.96F, 0.97F, 0.99F }; metallic = 1.0F; roughness = 0.04F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("24K Gold"))  { base_color = { 1.00F, 0.78F, 0.34F }; metallic = 1.0F; roughness = 0.14F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copper"))    { base_color = { 0.95F, 0.50F, 0.36F }; metallic = 0.95F; roughness = 0.25F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Ceramic"))   { base_color = { 0.92F, 0.95F, 0.98F }; metallic = 0.04F; roughness = 0.08F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Matte"))     { base_color = { 0.24F, 0.26F, 0.30F }; metallic = 0.05F; roughness = 0.85F; }
    }
};

/// Standalone 3D Rigid-Body Dynamics Modifier class (`cd::physics::BodyType`).
class RigidBodyModifier final : public IStudioModifier
{
public:
    cd::physics::BodyType body_type        { cd::physics::BodyType::kDynamic };
    float                 mass             { 2.0F };
    float                 gravity_scale    { 1.0F };
    float                 restitution      { 0.45F }; // Bounciness [0..1]
    float                 friction         { 0.55F }; // Coulomb friction [0..1]
    float                 linear_damping   { 0.04F };
    bool                  use_gravity      { true };
    cd::math::Vec3f       linear_velocity  { 0.0F, 0.0F, 0.0F };
    bool                  is_grounded      { false };

    explicit RigidBodyModifier(cd::physics::BodyType bt = cd::physics::BodyType::kDynamic,
                               float m = 2.0F,
                               float bounce = 0.45F) noexcept
        : body_type { bt }, mass { m }, restitution { bounce }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "RigidBodyModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "RigidBody 3D Physics"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kPhysics; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<RigidBodyModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        int bt = static_cast<int>(body_type);
        const char* bt_names[] = { "Static (Immovable Floor/Wall)", "Kinematic (Script/Gizmo Driven)", "Dynamic (Gravity & Forces)" };
        if (ImGui::Combo("Body Type", &bt, bt_names, 3))
        {
            body_type = static_cast<cd::physics::BodyType>(bt);
            if (body_type != cd::physics::BodyType::kDynamic)
                linear_velocity = { 0.0F, 0.0F, 0.0F };
        }
        if (body_type == cd::physics::BodyType::kDynamic)
        {
            ImGui::Checkbox("Use Gravity (-9.81 m/s^2)", &use_gravity);
            ImGui::SliderFloat("Mass (kg)", &mass, 0.10F, 100.0F, "%.2f");
            ImGui::SliderFloat("Gravity Scale", &gravity_scale, 0.0F, 3.0F, "%.2f");
        }
        ImGui::SliderFloat("Restitution (Bounce)", &restitution, 0.0F, 0.98F, "%.2f");
        ImGui::SliderFloat("Friction", &friction, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Linear Damping", &linear_damping, 0.0F, 0.50F, "%.3f");
        ImGui::TextDisabled("Velocity: (%.2f, %.2f, %.2f) m/s | %s",
                            static_cast<double>(linear_velocity.x),
                            static_cast<double>(linear_velocity.y),
                            static_cast<double>(linear_velocity.z),
                            is_grounded ? "GROUNDED" : "AIRBORNE");
        if (body_type == cd::physics::BodyType::kDynamic)
        {
            if (ImGui::SmallButton("Jump Impulse (+Y)"))
                linear_velocity.y += 6.2F;
            ImGui::SameLine();
            if (ImGui::SmallButton("Push (+X)"))
                linear_velocity.x += 4.5F;
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop"))
                linear_velocity = { 0.0F, 0.0F, 0.0F };
        }
    }
};

/// Collider shape family used by the Narrowphase Collision Detector.
enum class ColliderShapeKind : std::uint8_t
{
    kBox     = 0,
    kSphere  = 1,
    kCapsule = 2,
};

/// Base Collider Modifier with common collision telemetry & wireframe toggle.
class IColliderModifier : public IStudioModifier
{
public:
    cd::math::Vec3f center_offset       { 0.0F, 0.0F, 0.0F };
    bool            is_trigger          { false };
    bool            show_wireframe      { true };
    // Live per-frame collision telemetry populated by StudioPhysicsEngine:
    bool            currently_colliding { false };
    std::uint32_t   active_contacts     { 0 };
    cd::math::Vec3f last_contact_point  { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f last_contact_normal { 0.0F, 1.0F, 0.0F };
    float           last_penetration    { 0.0F };

    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kCollision; }
    [[nodiscard]] virtual ColliderShapeKind shape_kind() const noexcept = 0;

    void draw_common_collision_telemetry()
    {
        ImGui::DragFloat3("Center Offset", &center_offset.x, 0.02F, -10.0F, 10.0F, "%.2f");
        ImGui::Checkbox("Show 3D Collider Wireframe", &show_wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Is Trigger", &is_trigger);
        if (currently_colliding)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.38F, 0.22F, 1.0F),
                               "COLLIDING (%u contacts, depth=%.3f m, n=(%.2f,%.2f,%.2f))",
                               active_contacts,
                               static_cast<double>(last_penetration),
                               static_cast<double>(last_contact_normal.x),
                               static_cast<double>(last_contact_normal.y),
                               static_cast<double>(last_contact_normal.z));
        }
        else
        {
            ImGui::TextColored(ImVec4(0.30F, 0.92F, 0.45F, 1.0F),
                               "No Active Collision (Clear)");
        }
    }
};

/// Standalone 3D Box Collider Modifier class (`cd::physics::Aabb`).
class BoxColliderModifier final : public IColliderModifier
{
public:
    cd::math::Vec3f half_extents { 0.50F, 0.50F, 0.50F };

    explicit BoxColliderModifier(cd::math::Vec3f he = { 0.50F, 0.50F, 0.50F }) noexcept
        : half_extents { he }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "BoxColliderModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Box Collider 3D"; }
    [[nodiscard]] ColliderShapeKind shape_kind() const noexcept override { return ColliderShapeKind::kBox; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<BoxColliderModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::DragFloat3("Half Extents (m)", &half_extents.x, 0.02F, 0.05F, 50.0F, "%.2f");
        draw_common_collision_telemetry();
    }
};

/// Standalone 3D Sphere Collider Modifier class (`cd::physics::Sphere`).
class SphereColliderModifier final : public IColliderModifier
{
public:
    float radius { 0.50F };

    explicit SphereColliderModifier(float r = 0.50F) noexcept
        : radius { r }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "SphereColliderModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Sphere Collider 3D"; }
    [[nodiscard]] ColliderShapeKind shape_kind() const noexcept override { return ColliderShapeKind::kSphere; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<SphereColliderModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Sphere Radius (m)", &radius, 0.05F, 15.0F, "%.2f");
        draw_common_collision_telemetry();
    }
};

/// Standalone 3D Capsule Collider Modifier class (`cd::physics::Capsule`).
class CapsuleColliderModifier final : public IColliderModifier
{
public:
    float radius      { 0.38F };
    float half_height { 0.65F };

    CapsuleColliderModifier(float r = 0.38F, float hh = 0.65F) noexcept
        : radius { r }, half_height { hh }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "CapsuleColliderModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Capsule Collider 3D"; }
    [[nodiscard]] ColliderShapeKind shape_kind() const noexcept override { return ColliderShapeKind::kCapsule; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<CapsuleColliderModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Capsule Radius (m)", &radius, 0.05F, 5.0F, "%.2f");
        ImGui::SliderFloat("Capsule Half-Height (m)", &half_height, 0.05F, 10.0F, "%.2f");
        draw_common_collision_telemetry();
    }
};

/// Standalone Light Emitter Modifier class (attaches a physical light to an Entity).
class LightEmitterModifier final : public IStudioModifier
{
public:
    cd::light::LightType light_type { cd::light::LightType::kPoint };
    float                kelvin     { 4200.0F };
    cd::math::Vec3f      color      { 1.0F, 0.92F, 0.80F };
    float                intensity  { 4500.0F };
    float                range      { 14.0F };
    cd::math::Vec3f      direction  { 0.0F, -1.0F, 0.0F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "LightEmitterModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Light Emitter (Clustered PBR)"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kLighting; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<LightEmitterModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        int lt = static_cast<int>(light_type);
        const char* lt_names[] = { "Directional (Lux)", "Point (Lumens)", "Spot (Cone)", "Rect Area (LTC)" };
        if (ImGui::Combo("Light Type", &lt, lt_names, 4))
            light_type = static_cast<cd::light::LightType>(lt);
        if (ImGui::SliderFloat("Kelvin (CCT)", &kelvin, 1800.0F, 15000.0F, "%.0f K"))
            color = cd::light::cct_to_linear_rgb(kelvin);
        ImGui::ColorEdit3("Color Tint", &color.x);
        ImGui::SliderFloat("Intensity", &intensity, 10.0F, 25000.0F, "%.0f");
        ImGui::SliderFloat("Range (m)", &range, 1.0F, 40.0F, "%.1f");
    }
};

/// Standalone Character Controller Modifier class (Arrow keys + Space jump).
class CharacterControllerModifier final : public IStudioModifier
{
public:
    float move_speed   { 4.2F };
    float jump_impulse { 6.5F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "CharacterControllerModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Character Controller (Arrows+Space)"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kGameplay; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<CharacterControllerModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::SliderFloat("Walk Speed (m/s)", &move_speed, 0.5F, 15.0F, "%.1f");
        ImGui::SliderFloat("Jump Impulse", &jump_impulse, 1.0F, 15.0F, "%.1f");
        ImGui::TextDisabled("Use Arrow Keys to Move + Space/Up to Jump");
    }
};

// =============================================================================
// 3. Modifier Factory & Registry (Instantiate any Modifier Class by Name)
// =============================================================================
struct ModifierDescriptor
{
    const char*      class_id    {};
    const char*      title       {};
    const char*      short_badge {};
    ModifierCategory category    {};
    const char*      tooltip     {};
};

inline constexpr std::array<ModifierDescriptor, 11> kModifierCatalog {{
    { "RigidBodyModifier",           "RigidBody 3D",   "RB",   ModifierCategory::kPhysics,
      "Adds mass, gravity (-9.81 m/s^2), velocity, bounce restitution, and friction." },
    { "BoxColliderModifier",         "Box Collider",   "BOX",  ModifierCategory::kCollision,
      "Adds an Oriented 3D Box collision volume (OBB) with live wireframe and contact solver." },
    { "SphereColliderModifier",      "Sphere Col",     "SPH",  ModifierCategory::kCollision,
      "Adds a 3D Sphere collision volume with live wireframe and bounce contacts." },
    { "CapsuleColliderModifier",     "Capsule Col",    "CAP",  ModifierCategory::kCollision,
      "Adds a 3D Capsule collision volume for characters and pillars." },
    { "PbrMaterialModifier",         "PBR Material",   "PBR",  ModifierCategory::kMaterial,
      "Cook-Torrance metallic/roughness/albedo material modifier." },
    { "LightEmitterModifier",        "Light Emitter",  "LGT",  ModifierCategory::kLighting,
      "Attaches a real-time clustered Point/Spot/Area light to this entity." },
    { "CharacterControllerModifier", "Char Control",   "CHR",  ModifierCategory::kGameplay,
      "Drive this entity with Arrow Keys and jump with physics." },
    { "FogModifier",                 "Volumetric Fog", "FOG",  ModifierCategory::kEnvironment,
      "Instantiates volumetric fog, height falloff, and sun god-ray light shafts." },
    { "BloomLensModifier",           "Bloom & Lens",   "BLM",  ModifierCategory::kEnvironment,
      "Instantiates multi-mip HDR bloom, DoF, chromatic aberration, and vignette." },
    { "TonemapCameraModifier",       "Tonemap & Exp",  "TMP",  ModifierCategory::kEnvironment,
      "Instantiates exposure, saturation, ACES/Hable/AgX tonemapper, and TAA." },
    { "SkyAtmosphereModifier",       "Sky & DDGI GI",  "SKY",  ModifierCategory::kEnvironment,
      "Instantiates volumetric clouds, GTAO, SSR, and Ray-Traced DDGI GI." },
}};

[[nodiscard]] inline std::unique_ptr<IStudioModifier> instantiate_modifier_by_class(
    std::string_view class_id)
{
    if (class_id == "FogModifier")                 return std::make_unique<FogModifier>();
    if (class_id == "BloomLensModifier")           return std::make_unique<BloomLensModifier>();
    if (class_id == "TonemapCameraModifier")       return std::make_unique<TonemapCameraModifier>();
    if (class_id == "SkyAtmosphereModifier")       return std::make_unique<SkyAtmosphereModifier>();
    if (class_id == "PbrMaterialModifier")         return std::make_unique<PbrMaterialModifier>();
    if (class_id == "RigidBodyModifier")           return std::make_unique<RigidBodyModifier>();
    if (class_id == "BoxColliderModifier")         return std::make_unique<BoxColliderModifier>();
    if (class_id == "SphereColliderModifier")      return std::make_unique<SphereColliderModifier>();
    if (class_id == "CapsuleColliderModifier")     return std::make_unique<CapsuleColliderModifier>();
    if (class_id == "LightEmitterModifier")        return std::make_unique<LightEmitterModifier>();
    if (class_id == "CharacterControllerModifier") return std::make_unique<CharacterControllerModifier>();
    return nullptr;
}

// =============================================================================
// 4. Studio Modifier Stack & World Registry
// =============================================================================
struct ModifierStack
{
    std::vector<std::unique_ptr<IStudioModifier>> items {};

    template <typename ModT>
    [[nodiscard]] ModT* find() noexcept
    {
        for (auto& m : items)
        {
            if (auto* p = dynamic_cast<ModT*>(m.get())) return p;
        }
        return nullptr;
    }

    template <typename ModT>
    [[nodiscard]] const ModT* find() const noexcept
    {
        for (const auto& m : items)
        {
            if (const auto* p = dynamic_cast<const ModT*>(m.get())) return p;
        }
        return nullptr;
    }

    [[nodiscard]] IColliderModifier* find_collider() noexcept
    {
        for (auto& m : items)
        {
            if (auto* c = dynamic_cast<IColliderModifier*>(m.get()))
            {
                if (c->enabled) return c;
            }
        }
        return nullptr;
    }

    IStudioModifier* add_or_replace(std::unique_ptr<IStudioModifier> mod)
    {
        if (!mod) return nullptr;
        for (auto& existing : items)
        {
            if (existing->class_name() == mod->class_name())
            {
                existing = std::move(mod);
                return existing.get();
            }
        }
        items.push_back(std::move(mod));
        return items.back().get();
    }
};

struct ActiveContactDebug
{
    cd::math::Vec3f point       {};
    cd::math::Vec3f normal      {};
    float           penetration {};
};

struct StudioRuntimeWorld
{
    bool                                                  initialized               { false };
    bool                                                  clean_studio_mode         { true };
    bool                                                  simulate_physics_in_edit  { true };
    bool                                                  step_single_frame         { false };
    bool                                                  show_all_collider_wires   { true };
    bool                                                  grid_snap_enabled         { false };
    float                                                 grid_snap_step            { 0.50F };
    bool                                                  uniform_scale_lock        { false };
    float                                                 world_gravity_y           { -9.81F };
    std::unique_ptr<cd::physics::IPhysicsWorld>           physics_world             {};
    ModifierStack                                         environment_modifiers     {};
    std::unordered_map<std::uint32_t, ModifierStack>      entity_modifiers          {};
    std::unordered_map<std::uint32_t, cd::math::Transformf> home_transforms         {};
    std::unordered_map<std::uint32_t, cd::math::Vec3f>    euler_cache_deg           {};
    std::vector<ActiveContactDebug>                       live_contacts             {};
    // Drag-and-drop 3D preview state
    bool                                                  is_dragging_spawn         { false };
    std::string                                           drag_spawn_id             {};
    cd::math::Vec3f                                       drag_preview_pos          { 0.0F, 0.0F, 0.0F };
    bool                                                  is_dragging_modifier      { false };
    std::string                                           drag_modifier_class       {};
    int                                                   drag_hover_entity_idx     { -1 };
    // Right-click context menu & hierarchy filter
    ImVec2                                                rmb_down_pos              { 0.0F, 0.0F };
    cd::math::Vec3f                                       context_menu_world_pos    { 0.0F, 0.0F, 0.0F };
    char                                                  hierarchy_filter[64]      {};
};

inline StudioRuntimeWorld& studio_world() noexcept
{
    static StudioRuntimeWorld s_world {};
    return s_world;
}

// =============================================================================

} // namespace cd::studio
