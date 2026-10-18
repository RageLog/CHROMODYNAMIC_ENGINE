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

#include "StudioNodeCatalog.hpp"

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
#include <cd/physics/RockstarPhysicsWorldAdapter.hpp>
#include <cd/physics/Sphere.hpp>
#include <cd/physics/UnifiedPhysicsEngine.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <cd/scene/constructs/ChromaConstructRegistry.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/world_container/LayerMember.hpp>
#include <cd/world_container/ProjectIo.hpp>
#include <cd/world_container/World.hpp>

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
    float taa_amount       { 0.00F };
    float motion_blur      { 0.00F };

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

/// Standalone Cook-Torrance PBR Material Modifier class for any 3D Entity
/// (enhanced with Turquin 2019 Multi-Scattering Energy Compensation & Belcour-Barla 2017 Thin-Film Iridescence).
class PbrMaterialModifier final : public IStudioModifier
{
public:
    cd::math::Vec3f base_color         { 0.85F, 0.86F, 0.88F };
    float           metallic           { 0.15F };
    float           roughness          { 0.35F };
    bool            use_texture        { false };
    int             texture_slot       { 0 };    // 0=None, 1=Earth/Disk, 2=Checker, 3=Brick, 4=Carbon, 5=Marble, 6=Wood
    std::string     texture_asset_path {};
    float           multiscatter_comp  { 1.0F }; // Turquin (2019) energy compensation [0..1]
    float           iridescence_nm     { 0.0F }; // Belcour-Barla (2017) thin-film thickness [0..1000 nm]

    PbrMaterialModifier() noexcept = default;
    PbrMaterialModifier(cd::math::Vec3f col, float m, float r, bool tex = false,
                        float ms = 1.0F, float irid_nm = 0.0F, int tex_slot = 0,
                        std::string tex_path = {}) noexcept
        : base_color { col }, metallic { m }, roughness { r }, use_texture { tex }
        , texture_slot { tex_slot != 0 ? tex_slot : (tex ? 1 : 0) }
        , texture_asset_path { std::move(tex_path) }
        , multiscatter_comp { ms }, iridescence_nm { irid_nm }
    {
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "PbrMaterialModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "PBR Material (Turquin MS + Irid)"; }
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
        ImGui::SliderFloat("Turquin Multi-Scatter", &multiscatter_comp, 0.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Thin-Film Iridescence (nm)", &iridescence_nm, 0.0F, 800.0F, "%.0f nm");
        const char* tex_slots[] = {
            "0: None (Solid Tint)",
            "1: Earth / Disk Image",
            "2: UV Checkerboard 2K",
            "3: Brick & Mortar Wall",
            "4: Carbon Fiber Twill",
            "5: Carrara Vein Marble",
            "6: Warm Oak Wood Planks"
        };
        if (ImGui::Combo("Texture Map", &texture_slot, tex_slots, 7))
        {
            use_texture = (texture_slot > 0);
        }
        if (!texture_asset_path.empty())
        {
            ImGui::TextDisabled("Bound Asset: %s", texture_asset_path.c_str());
        }
        ImGui::TextDisabled("Quick Surface Presets:");
        if (ImGui::SmallButton("Chrome"))    { base_color = { 0.96F, 0.97F, 0.99F }; metallic = 1.0F; roughness = 0.04F; multiscatter_comp = 1.0F; iridescence_nm = 0.0F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("24K Gold"))  { base_color = { 1.00F, 0.78F, 0.34F }; metallic = 1.0F; roughness = 0.14F; multiscatter_comp = 1.0F; iridescence_nm = 0.0F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rough Gold")) { base_color = { 1.00F, 0.78F, 0.34F }; metallic = 1.0F; roughness = 0.78F; multiscatter_comp = 1.0F; iridescence_nm = 0.0F; }
        if (ImGui::SmallButton("Bismuth (380nm)"))    { base_color = { 0.88F, 0.90F, 0.95F }; metallic = 1.0F; roughness = 0.18F; multiscatter_comp = 1.0F; iridescence_nm = 380.0F; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Soap Film (520nm)"))  { base_color = { 0.92F, 0.95F, 0.98F }; metallic = 0.15F; roughness = 0.08F; multiscatter_comp = 1.0F; iridescence_nm = 520.0F; }
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

/// Godot+ Kinematic Mover / Oscillating Platform & Rotator Modifier (`MOV`).
class KinematicMoverModifier final : public IStudioModifier
{
public:
    cd::math::Vec3f move_axis           { 1.0F, 0.0F, 0.0F };
    float           amplitude           { 3.5F };
    float           frequency_hz        { 0.35F };
    float           phase_offset        { 0.0F };
    cd::math::Vec3f angular_deg_per_sec { 0.0F, 0.0F, 0.0F };
    bool            carry_riders        { true };
    cd::math::Vec3f last_frame_delta    { 0.0F, 0.0F, 0.0F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "KinematicMoverModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Kinematic Mover / Platform 3D"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kGameplay; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<KinematicMoverModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        ImGui::DragFloat3("Move Axis", &move_axis.x, 0.05F, -1.0F, 1.0F, "%.2f");
        ImGui::SliderFloat("Amplitude (m)", &amplitude, 0.0F, 20.0F, "%.2f");
        ImGui::SliderFloat("Speed (Hz)", &frequency_hz, 0.05F, 3.0F, "%.2f");
        ImGui::SliderFloat("Phase (rad)", &phase_offset, 0.0F, 6.28F, "%.2f");
        ImGui::DragFloat3("Spin (deg/s)", &angular_deg_per_sec.x, 1.0F, -360.0F, 360.0F, "%.1f");
        ImGui::Checkbox("Carry Standing Riders", &carry_riders);
    }
};

/// Gameplay Trigger Volume Action Kind.
enum class TriggerActionKind : int
{
    kCollectibleScore = 0,
    kJumpPadBoost     = 1,
    kHazardDamage     = 2,
    kHealStation      = 3,
    kLevelGoalWin     = 4,
};

/// Godot+ Gameplay Trigger Volume Sensor Modifier (`TRG`).
class TriggerVolumeModifier final : public IStudioModifier
{
public:
    TriggerActionKind action         { TriggerActionKind::kCollectibleScore };
    float             effect_value   { 100.0F };
    float             trigger_radius { 1.25F };
    bool              one_shot       { true };
    bool              triggered      { false };
    float             cooldown_sec   { 0.0F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "TriggerVolumeModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Gameplay Trigger Volume 3D"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kGameplay; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<TriggerVolumeModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        int act = static_cast<int>(action);
        const char* act_names[] = {
            "Collectible (+Score & Consume)",
            "Jump Pad (+Y Velocity Boost)",
            "Hazard Zone (-HP Damage)",
            "Heal / Shield Station (+HP)",
            "Level Goal Beacon (Victory!)"
        };
        if (ImGui::Combo("Trigger Action", &act, act_names, 5))
            action = static_cast<TriggerActionKind>(act);
        ImGui::SliderFloat("Effect Value", &effect_value, 1.0F, 500.0F, "%.1f");
        ImGui::SliderFloat("Trigger Radius (m)", &trigger_radius, 0.25F, 8.0F, "%.2f");
        ImGui::Checkbox("One-Shot (Consume on Hit)", &one_shot);
        if (triggered)
        {
            ImGui::TextColored(ImVec4(1.0F, 0.78F, 0.22F, 1.0F), "Status: TRIGGERED");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Trigger"))
                triggered = false;
        }
        else
        {
            ImGui::TextColored(ImVec4(0.30F, 0.92F, 0.50F, 1.0F), "Status: ARMED (Ready)");
        }
    }
};

/// Godot+ Autonomous AI Waypoint Patrol / Synth Bot Modifier (`AI`).
class PatrolWaypointModifier final : public IStudioModifier
{
public:
    int   patrol_axis    { 0 }; // 0=X, 1=Y, 2=Z
    float patrol_span    { 4.0F };
    float move_speed     { 2.6F };
    bool  chase_player   { true };
    float chase_range    { 6.0F };
    float direction_sign { 1.0F };

    [[nodiscard]] std::string_view class_name() const noexcept override { return "PatrolWaypointModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "AI Waypoint Patrol / Synth Bot"; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kGameplay; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        return std::make_unique<PatrolWaypointModifier>(*this);
    }

    void draw_inspector_ui() override
    {
        const char* axes[] = { "X Axis (Left-Right)", "Y Axis (Up-Down)", "Z Axis (Forward-Back)" };
        ImGui::Combo("Patrol Axis", &patrol_axis, axes, 3);
        ImGui::SliderFloat("Patrol Span (m)", &patrol_span, 0.5F, 20.0F, "%.1f");
        ImGui::SliderFloat("Move Speed (m/s)", &move_speed, 0.5F, 12.0F, "%.1f");
        ImGui::Checkbox("Chase Player in Range", &chase_player);
        if (chase_player)
            ImGui::SliderFloat("Detect Range (m)", &chase_range, 1.0F, 20.0F, "%.1f");
    }
};

/// Universal 250-Node Catalog Trait Capsule (`219 Standard + 31 Next-Gen SOTA`).
/// Can be attached to any entity viaSpawnRootElement, NestAsChildElement, or FoldAsInlineCapsule.
class UniversalNodeModifier final : public IStudioModifier
{
public:
    std::uint16_t       node_id            { 117 }; // Default: CharacterBody3D / AvatarVessel3D
    std::string         godot_name         { "CharacterBody3D" };
    std::string         chroma_name        { "AvatarVessel3D" };
    std::string         video_timestamp    { "17:44" };
    std::string         ux_upgrade         { "Inline Capsule + SpringArm + GroundSensor + CoyoteJump in 1 Vessel" };
    std::string         tech_upgrade       { "60Hz Sub-Stepped Kinematic Sweep + Slope/Stair Solver" };
    StudioNodeCategory  catalog_category   { StudioNodeCategory::Node3D };
    ChromaConstructKind construct_kind     { ChromaConstructKind::Vessel };
    float               intensity_or_rate  { 1.0F };
    int                 substeps_or_quality{ 4 };
    bool                gpu_accelerated    { true };
    std::string         display_label_buf  { "VSL: AvatarVessel3D (CharacterBody3D)" };
    std::shared_ptr<cd::scene::IChromaConstruct> construct_instance {
        cd::scene::instantiate_chroma_construct(117u)
    };

    UniversalNodeModifier() = default;

    explicit UniversalNodeModifier(const StudioNodeDescriptor& desc)
        : node_id { desc.id }
        , godot_name { desc.godot_name }
        , chroma_name { desc.chroma_name }
        , video_timestamp { desc.video_timestamp }
        , ux_upgrade { desc.ux_upgrade }
        , tech_upgrade { desc.tech_upgrade }
        , catalog_category { desc.category }
        , construct_kind { desc.construct_kind }
        , construct_instance { cd::scene::instantiate_chroma_construct(desc.id) }
    {
        rebuild_label();
    }

    void rebuild_label()
    {
        display_label_buf = std::string(chroma_construct_kind_badge(construct_kind)) + ": " +
                            chroma_name + " (" + godot_name + ")";
    }

    void bind_descriptor(const StudioNodeDescriptor& desc)
    {
        node_id            = desc.id;
        godot_name         = std::string(desc.godot_name);
        chroma_name        = std::string(desc.chroma_name);
        video_timestamp    = std::string(desc.video_timestamp);
        ux_upgrade         = std::string(desc.ux_upgrade);
        tech_upgrade       = std::string(desc.tech_upgrade);
        catalog_category   = desc.category;
        construct_kind     = desc.construct_kind;
        construct_instance = cd::scene::instantiate_chroma_construct(desc.id);
        rebuild_label();
    }

    [[nodiscard]] std::string_view class_name() const noexcept override { return "UniversalNodeModifier"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return display_label_buf; }
    [[nodiscard]] ModifierCategory category() const noexcept override { return ModifierCategory::kGameplay; }

    [[nodiscard]] std::unique_ptr<IStudioModifier> clone() const override
    {
        auto copy = std::make_unique<UniversalNodeModifier>(*this);
        if (construct_instance)
        {
            copy->construct_instance = std::shared_ptr<cd::scene::IChromaConstruct>(
                construct_instance->clone().release());
        }
        return copy;
    }

    void draw_inspector_ui() override
    {
        int id_edit = static_cast<int>(node_id);
        if (ImGui::SliderInt("Catalog Construct ID", &id_edit, 0, static_cast<int>(kTotalStudioNodeCount - 1)))
        {
            if (const auto* desc = find_studio_node_by_id(static_cast<std::uint16_t>(id_edit)))
                bind_descriptor(*desc);
        }
        ImGui::TextColored(ImVec4(0.32F, 0.94F, 0.68F, 1.0F),
                           "[%s #%u] class cd::scene::%s",
                           chroma_construct_kind_badge(construct_kind),
                           static_cast<unsigned>(node_id),
                           chroma_name.c_str());
        ImGui::TextColored(ImVec4(0.55F, 0.82F, 1.0F, 1.0F),
                           "Standard Construct: %s | Ref @ %s | %s",
                           godot_name.c_str(),
                           video_timestamp.c_str(),
                           studio_node_category_label(catalog_category));
        ImGui::TextDisabled("UX Upgrade: %s", ux_upgrade.c_str());
        ImGui::TextDisabled("SOTA Tech:  %s", tech_upgrade.c_str());
        ImGui::SliderFloat("Capsule Gain / Rate", &intensity_or_rate, 0.05F, 10.0F, "%.2f");
        ImGui::SliderInt("Solver Substeps / LOD", &substeps_or_quality, 1, 16);
        ImGui::Checkbox("GPU Compute / SIMD Fast-Path", &gpu_accelerated);
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

inline constexpr std::array<ModifierDescriptor, 15> kModifierCatalog {{
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
    { "KinematicMoverModifier",      "Mover Platform", "MOV",  ModifierCategory::kGameplay,
      "Oscillating/rotating kinematic platform, elevator, or obstacle that carries riders." },
    { "TriggerVolumeModifier",       "Trigger Zone",   "TRG",  ModifierCategory::kGameplay,
      "Overlap sensor for Collectibles (+Score), JumpPads (+Y Boost), Hazards (-HP), or Goal Beacons." },
    { "PatrolWaypointModifier",      "AI Patrol Bot",  "AI",   ModifierCategory::kGameplay,
      "Autonomous waypoint patrol & player-pursuit AI behavior for enemies and synth bots." },
    { "UniversalNodeModifier",       "Chroma Capsule", "CAP+", ModifierCategory::kGameplay,
      "Any of the 250 Universal Catalog Constructs (219 Standard Constructs + 31 Next-Gen SOTA) folded inline." },
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
    if (class_id == "KinematicMoverModifier")      return std::make_unique<KinematicMoverModifier>();
    if (class_id == "TriggerVolumeModifier")       return std::make_unique<TriggerVolumeModifier>();
    if (class_id == "PatrolWaypointModifier")      return std::make_unique<PatrolWaypointModifier>();
    if (class_id == "UniversalNodeModifier")       return std::make_unique<UniversalNodeModifier>();
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

    [[nodiscard]] UniversalNodeModifier* find_universal_node(std::uint16_t node_id) noexcept
    {
        for (auto& m : items)
        {
            if (auto* u = dynamic_cast<UniversalNodeModifier*>(m.get()))
            {
                if (u->node_id == node_id) return u;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const UniversalNodeModifier* find_universal_node(std::uint16_t node_id) const noexcept
    {
        for (const auto& m : items)
        {
            if (const auto* u = dynamic_cast<const UniversalNodeModifier*>(m.get()))
            {
                if (u->node_id == node_id) return u;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t count_universal_nodes() const noexcept
    {
        std::size_t count = 0;
        for (const auto& m : items)
        {
            if (dynamic_cast<const UniversalNodeModifier*>(m.get()) != nullptr)
                ++count;
        }
        return count;
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
        const auto* incoming_un = dynamic_cast<const UniversalNodeModifier*>(mod.get());
        for (auto& existing : items)
        {
            if (existing->class_name() == mod->class_name())
            {
                if (incoming_un != nullptr)
                {
                    const auto* existing_un = dynamic_cast<const UniversalNodeModifier*>(existing.get());
                    if (existing_un != nullptr && existing_un->node_id != incoming_un->node_id)
                    {
                        continue; // Allow multiple distinct UniversalNodeModifier capsules on the same entity
                    }
                }
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

struct ContextAssetItem
{
    std::string              name       {};
    std::string              path       {};
    int                      kind       { 1 }; // 1 = 3D Object/Mesh, 2 = Texture, 3 = Material, 4 = Project/Scene
    cd::ui::chroma::IconKind icon       { cd::ui::chroma::IconKind::kMesh3D };
    std::uint32_t            accent_col { 0xFFFFB446U };
};

struct StudioRuntimeWorld
{
    bool                                                  initialized               { false };
    bool                                                  clean_studio_mode         { true };
    bool                                                  simulate_physics_in_edit  { true };
    bool                                                  step_single_frame         { false };
    bool                                                  show_all_collider_wires   { false };
    bool                                                  grid_snap_enabled         { false };
    float                                                 grid_snap_step            { 0.50F };
    bool                                                  uniform_scale_lock        { false };
    float                                                 world_gravity_y           { -9.81F };
    std::unique_ptr<cd::physics::IPhysicsWorld>           physics_world             {};
    cd::physics::UnifiedPhysicsEngine                     rockstar_physics_engine   {};
    bool                                                  rockstar_engine_seeded    { false };
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
    // Real-time smoothed FPS & Frame Profiler telemetry
    float                                                 smoothed_fps              { 0.0F };
    float                                                 smoothed_frame_ms         { 1.0F };
    float                                                 peak_fps                  { 0.0F };
    std::array<float, 64>                                 frame_ms_history          {};
    std::size_t                                           frame_ms_cursor           { 0 };
    // Multi-Project, Multi-World, Multi-Level & Layer Ecosystem state (Starts as Clean Empty Project!)
    std::string                                           active_project_name       { "Empty_Project" };
    std::string                                           active_world_name         { "Default_World" };
    std::string                                           active_level_name         { "Empty_Stage" };
    std::string                                           active_layer_name         { "Default" };
    char                                                  new_layer_name_buf[32]    { "Gameplay" };
    char                                                  new_level_name_buf[48]    { "Custom_Arena" };
    // Disk Explorer -> Active Context Two-Stage Asset Pipeline state
    std::string                                           current_content_folder    { "assets/meshes" };
    char                                                  content_search_filter[64] {};
    int                                                   content_type_filter       { 0 }; // 0=All, 1=Objects/Meshes, 2=Textures, 3=Materials, 4=Projects
    std::string                                           selected_content_asset    { "assets/meshes/Sphere.mesh" };
    std::string                                           last_asset_status_msg     { "Empty Project Ready — Click or Drag 3D Objects from Context to Spawn at (0, 0, 0)" };
    std::vector<ContextAssetItem>                         loaded_context_assets     {};
    int                                                   selected_context_idx      { -1 };
    bool                                                  is_dragging_asset_mesh    { false };
    std::string                                           drag_asset_mesh_id        {};
    bool                                                  is_dragging_asset_texture { false };
    std::string                                           drag_asset_texture_id     {};
    // 250 Element & Vessel Universal Catalog UI state (219 Standard + 31 Next-Gen SOTA)
    int                                                   node_catalog_category_filter { -1 }; // -1=All 250, 0=Core(21), 1=2D(50), 2=3D(88), 3=UI(60), 4=SOTA(31)
    char                                                  node_catalog_search[64]      {};
    std::uint16_t                                         selected_catalog_node_id     { 117 }; // CharacterBody3D / PhysicsCharacterVessel3D
    bool                                                  focus_bottom_node_catalog_tab{ false };
    // Live Gameplay & Pre-PIE Snapshot / Restore State
    float                                                 sim_time_accum            { 0.0F };
    float                                                 player_hp                 { 100.0F };
    float                                                 player_max_hp             { 100.0F };
    float                                                 player_shield             { 100.0F };
    float                                                 player_energy             { 100.0F };
    int                                                   game_score                { 0 };
    int                                                   collected_cores           { 0 };
    int                                                   total_cores_in_level      { 0 };
    int                                                   defeated_enemies          { 0 };
    bool                                                  level_goal_reached        { false };
    bool                                                  autopilot_play_active     { false };
    float                                                 autopilot_fire_cooldown   { 0.0F };
    float                                                 player_heading_yaw_deg    { 90.0F };
    float                                                 player_horiz_speed        { 0.0F };
    std::string                                           hud_toast_message         { "Ready - Press F5 or [> PLAY] to test gameplay!" };
    float                                                 hud_toast_timer           { 0.0F };
    std::string                                           pre_pie_scene_snapshot    {};
    std::uint32_t                                         pre_pie_selected_id       { 0 };
    bool                                                  show_project_manager      { false };
    bool                                                  enable_demo_hud_overlay   { false };
    int                                                   project_manager_tab       { 0 };     // 0 = Projects, 1 = Templates
    int                                                   selected_pm_project_idx   { 0 };
    char                                                  pm_filter_buf[64]         {};
    char                                                  pm_new_project_buf[64]    { "My_Chroma_Project" };
    bool                                                  pm_creating_new           { false };

    [[nodiscard]] bool is_asset_in_context(std::string_view path) const noexcept
    {
        for (const auto& item : loaded_context_assets)
        {
            if (item.path == path) return true;
        }
        return false;
    }

    bool load_asset_into_context(const ContextAssetItem& item)
    {
        for (std::size_t i = 0; i < loaded_context_assets.size(); ++i)
        {
            if (loaded_context_assets[i].path == item.path)
            {
                selected_context_idx = static_cast<int>(i);
                return false;
            }
        }
        loaded_context_assets.push_back(item);
        selected_context_idx = static_cast<int>(loaded_context_assets.size()) - 1;
        return true;
    }

    bool load_asset_into_context(std::string_view name, std::string_view path, std::uint8_t kind)
    {
        ContextAssetItem item {};
        item.name = std::string(name);
        item.path = std::string(path);
        item.kind = kind;
        return load_asset_into_context(item);
    }
};

inline StudioRuntimeWorld& studio_world() noexcept
{
    static StudioRuntimeWorld s_world {};
    return s_world;
}

// =============================================================================

} // namespace cd::studio
