// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioNodeCatalog.hpp
//
// Universal 250-Element & Vessel Registry (219 Godot Video Equivalents from
// `https://www.youtube.com/watch?v=tO2gthp45MA` + 31 Next-Gen SOTA Beyond-Godot
// Capabilities).
//
// Architectural Identity:
//   * Uses CHROMODYNAMIC's native `Element`, `Vessel`, `Pilot`, `Stage`,
//     `TraitCapsule`, and `PulseBus` architecture instead of rigid single-purpose
//     inheritance trees.
//   * Supports 3 Composition Modes directly in the Editor & API:
//       1. Spawn Root Element/Vessel (`CatalogComposeMode::SpawnRootElement`)
//          -- creates a self-contained entity with its inline Trait Capsules
//          (Collider, RigidBody, PBR/Turquin Material, Controller, Light)
//          already bundled in 1 click (zero Godot child-node boilerplate!).
//       2. Nest as Child Element (`CatalogComposeMode::NestAsChildElement`)
//          -- spawns and attaches as a nested child under the currently selected
//          Element/Vessel in the Scene Hierarchy with local transform offset.
//       3. Fold as Inline Capsule (`CatalogComposeMode::FoldAsInlineCapsule`)
//          -- folds the capability directly into the selected Element/Vessel's
//          inline ModifierStack without cluttering the scene hierarchy tree.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cd::studio
{

enum class StudioNodeCategory : std::uint8_t
{
    CoreSystem  = 0, ///< 21 Base & System / Forgotten Nodes (0..20)
    Node2D      = 1, ///< 50 Planar 2D Elements & Vessels (21..70)
    Node3D      = 2, ///< 88 Spatial 3D Elements, Vessels & Probes (71..158)
    ControlUI   = 3, ///< 60 ChromaUI Controls & Containers (159..218)
    NextGenSOTA = 4, ///< 31 Beyond-Godot Next-Gen SOTA Capabilities (219..249)
};

enum class ChromaConstructKind : std::uint8_t
{
    Element      = 0, ///< Self-contained scene building block (`cd::element_core::Element`)
    Vessel       = 1, ///< Physical body driven by a Pilot (`AvatarVessel3D`, `RoverVessel3D`)
    Pilot        = 2, ///< Input or AI brain (`HumanPilot`, `SynthPilot`, `WayfinderAgent3D`)
    Stage        = 3, ///< World/Viewport/Layer container (`Stage`, `HudStageLayer`)
    TraitCapsule = 4, ///< Composable inline capability (`SpringArmCapsule3D`, `CollisionHullCapsule3D`)
    PulseBus     = 5, ///< Type-safe signal/sync/scheduler (`DeterministicPulseTimer`, `NetDeltaStateSynchronizer`)
};

enum class CatalogComposeMode : std::uint8_t
{
    SpawnRootElement   = 0, ///< Spawn self-contained Root Element/Vessel with inline capsules
    NestAsChildElement = 1, ///< Spawn & nest as child Element under currently selected parent
    FoldAsInlineCapsule = 2 ///< Fold capability directly into selected Element's inline stack
};

enum class CatalogPrimKind : std::uint8_t
{
    Cube       = 0,
    Sphere     = 1,
    Cone       = 2,
    Cylinder   = 3,
    Torus      = 4,
    TorusKnot  = 5,
    GltfAvatar = 6,
    Sponza     = 7,
};

namespace NodeSpawnFlags
{
    inline constexpr std::uint32_t kNone                = 0u;
    inline constexpr std::uint32_t kPbrMaterial         = 1u << 0;
    inline constexpr std::uint32_t kRigidBodyDynamic    = 1u << 1;
    inline constexpr std::uint32_t kRigidBodyStatic     = 1u << 2;
    inline constexpr std::uint32_t kRigidBodyKinematic  = 1u << 3;
    inline constexpr std::uint32_t kBoxCollider         = 1u << 4;
    inline constexpr std::uint32_t kSphereCollider      = 1u << 5;
    inline constexpr std::uint32_t kCapsuleCollider     = 1u << 6;
    inline constexpr std::uint32_t kCharacterController = 1u << 7;
    inline constexpr std::uint32_t kKinematicMover      = 1u << 8;
    inline constexpr std::uint32_t kTriggerCollectible  = 1u << 9;
    inline constexpr std::uint32_t kTriggerJumpPad      = 1u << 10;
    inline constexpr std::uint32_t kTriggerHazard       = 1u << 11;
    inline constexpr std::uint32_t kTriggerGoal         = 1u << 12;
    inline constexpr std::uint32_t kPatrolAi            = 1u << 13;
    inline constexpr std::uint32_t kLightPoint          = 1u << 14;
    inline constexpr std::uint32_t kLightSpot           = 1u << 15;
    inline constexpr std::uint32_t kLightDirectional    = 1u << 16;
    inline constexpr std::uint32_t kEnvFog              = 1u << 17;
    inline constexpr std::uint32_t kEnvBloomLens        = 1u << 18;
    inline constexpr std::uint32_t kEnvTonemap          = 1u << 19;
    inline constexpr std::uint32_t kEnvSkyDdgi          = 1u << 20;
    inline constexpr std::uint32_t kPlanar2DBinding     = 1u << 21;
    inline constexpr std::uint32_t kChromaUiBinding     = 1u << 22;
    inline constexpr std::uint32_t kSotaGpuCompute      = 1u << 23;
} // namespace NodeSpawnFlags

struct StudioNodeSpawnPreset
{
    CatalogPrimKind prim_kind         { CatalogPrimKind::Cube };
    cd::math::Vec3f scale             { 1.0F, 1.0F, 1.0F };
    cd::math::Vec3f albedo            { 0.80F, 0.84F, 0.90F };
    float           metallic          { 0.20F };
    float           roughness         { 0.35F };
    float           multiscatter_comp { 1.00F };
    float           iridescence_nm    { 0.00F };
    int             texture_slot      { 0 };
    std::uint32_t   modifier_flags    { NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider };
    const char*     semantic_layer    { "Default" };
    const char*     bundled_summary   { "PbrMaterial + BoxCollider3D + InlineCapsule" };
};

struct StudioNodeDescriptor
{
    std::uint16_t         id              {};
    const char*           godot_name      {};
    const char*           chroma_name     {};
    StudioNodeCategory    category        { StudioNodeCategory::CoreSystem };
    ChromaConstructKind   construct_kind  { ChromaConstructKind::Element };
    const char*           video_timestamp {};
    const char*           ux_upgrade      {};
    const char*           tech_upgrade    {};
    StudioNodeSpawnPreset spawn_preset    {};

    [[nodiscard]] constexpr const char* godot_equivalent() const noexcept { return godot_name; }
};

[[nodiscard]] constexpr const char* studio_node_category_label(StudioNodeCategory c) noexcept
{
    switch (c)
    {
        case StudioNodeCategory::CoreSystem:  return "Base & System (21)";
        case StudioNodeCategory::Node2D:      return "Planar 2D (50)";
        case StudioNodeCategory::Node3D:      return "Spatial 3D (88)";
        case StudioNodeCategory::ControlUI:   return "ChromaUI Control (60)";
        case StudioNodeCategory::NextGenSOTA: return "Next-Gen SOTA (31)";
    }
    return "Universal";
}

[[nodiscard]] constexpr const char* chroma_construct_kind_label(ChromaConstructKind k) noexcept
{
    switch (k)
    {
        case ChromaConstructKind::Element:      return "Element";
        case ChromaConstructKind::Vessel:       return "Vessel";
        case ChromaConstructKind::Pilot:        return "Pilot";
        case ChromaConstructKind::Stage:        return "Stage";
        case ChromaConstructKind::TraitCapsule: return "TraitCapsule";
        case ChromaConstructKind::PulseBus:     return "PulseBus";
    }
    return "Element";
}

[[nodiscard]] constexpr const char* chroma_construct_kind_badge(ChromaConstructKind k) noexcept
{
    switch (k)
    {
        case ChromaConstructKind::Element:      return "ELM";
        case ChromaConstructKind::Vessel:       return "VSL";
        case ChromaConstructKind::Pilot:        return "PLT";
        case ChromaConstructKind::Stage:        return "STG";
        case ChromaConstructKind::TraitCapsule: return "CAP";
        case ChromaConstructKind::PulseBus:     return "PLS";
    }
    return "ELM";
}

inline constexpr std::size_t kGodotBaseNodeCount         = 21U;
inline constexpr std::size_t kGodot2DNodeCount           = 50U;
inline constexpr std::size_t kGodot3DNodeCount           = 88U;
inline constexpr std::size_t kGodotUINodeCount           = 60U;
inline constexpr std::size_t kGodotTotalNodeCount        = 219U;
inline constexpr std::size_t kNextGenSotaNodeCount       = 31U;
inline constexpr std::size_t kTotalStudioNodeCount       = 250U;
inline constexpr std::size_t kStudioGodotVideoNodeCount  = kGodotTotalNodeCount;
inline constexpr std::size_t kStudioNextGenSotaNodeCount = kNextGenSotaNodeCount;
inline constexpr std::size_t kStudioTotalCatalogCount    = kTotalStudioNodeCount;

static_assert(kGodotBaseNodeCount + kGodot2DNodeCount + kGodot3DNodeCount + kGodotUINodeCount == kGodotTotalNodeCount,
              "Godot video node counts must sum to exactly 219");
static_assert(kGodotTotalNodeCount + kNextGenSotaNodeCount == kTotalStudioNodeCount,
              "Universal catalog must contain 219 Godot equivalents + 31 Next-Gen SOTA = 250 total");

// Short helper builders for concise constexpr preset initialization:
namespace detail_preset
{
    using namespace NodeSpawnFlags;

    [[nodiscard]] constexpr StudioNodeSpawnPreset make_sys(
        CatalogPrimKind pk, cd::math::Vec3f col, std::uint32_t extra_flags, const char* summary) noexcept
    {
        return StudioNodeSpawnPreset {
            pk, { 0.85F, 0.85F, 0.85F }, col, 0.45F, 0.25F, 1.0F, 0.0F, 0,
            kPbrMaterial | kBoxCollider | extra_flags, "Default", summary
        };
    }

    [[nodiscard]] constexpr StudioNodeSpawnPreset make_2d(
        CatalogPrimKind pk, cd::math::Vec3f col, std::uint32_t extra_flags, const char* summary, int tex = 2) noexcept
    {
        return StudioNodeSpawnPreset {
            pk, { 1.10F, 1.10F, 0.18F }, col, 0.12F, 0.28F, 1.0F, 0.0F, tex,
            kPbrMaterial | kPlanar2DBinding | extra_flags, "Geometry", summary
        };
    }

    [[nodiscard]] constexpr StudioNodeSpawnPreset make_3d(
        CatalogPrimKind pk, cd::math::Vec3f sc, cd::math::Vec3f col,
        float met, float rgh, float irid, int tex, std::uint32_t flags,
        const char* layer, const char* summary) noexcept
    {
        return StudioNodeSpawnPreset {
            pk, sc, col, met, rgh, 1.0F, irid, tex, flags, layer, summary
        };
    }

    [[nodiscard]] constexpr StudioNodeSpawnPreset make_ui(
        cd::math::Vec3f col, const char* summary) noexcept
    {
        return StudioNodeSpawnPreset {
            CatalogPrimKind::Cube, { 1.25F, 0.75F, 0.12F }, col, 0.20F, 0.22F, 1.0F, 0.0F, 0,
            kPbrMaterial | kBoxCollider | kChromaUiBinding, "UI", summary
        };
    }

    [[nodiscard]] constexpr StudioNodeSpawnPreset make_sota(
        CatalogPrimKind pk, cd::math::Vec3f sc, cd::math::Vec3f col,
        float met, float rgh, float irid, int tex, std::uint32_t extra_flags,
        const char* layer, const char* summary) noexcept
    {
        return StudioNodeSpawnPreset {
            pk, sc, col, met, rgh, 1.0F, irid, tex,
            kPbrMaterial | kSotaGpuCompute | extra_flags, layer, summary
        };
    }
} // namespace detail_preset

inline constexpr std::array<StudioNodeDescriptor, kTotalStudioNodeCount> kStudioNodeCatalog {{
    // =========================================================================
    // 1. BASE & SYSTEM / FORGOTTEN NODES (21 Nodes, IDs 0..20)
    // =========================================================================
    { 0, "Node", "ChromaElement", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "02:06",
      "Supports both nested child hierarchies AND zero-overhead inline Trait Capsules on one Element",
      "64-bit generational ECS Entity handle + constexpr reflection schema + zero-alloc Pulse<Args...> bus",
      detail_preset::make_sys(CatalogPrimKind::Cube, { 0.75F, 0.82F, 0.92F }, 0u, "ChromaElement + Reflection + PulseBus") },

    { 1, "Viewport", "StageViewportTarget", StudioNodeCategory::CoreSystem, ChromaConstructKind::Stage, "37:08",
      "1-click HDR render target with built-in ACES/AgX tonemap and automatic resize synchronization",
      "Vulkan 1.3 FrameGraph transient attachment aliasing + bindless SRV export",
      detail_preset::make_sys(CatalogPrimKind::Cube, { 0.35F, 0.78F, 0.96F }, NodeSpawnFlags::kEnvTonemap, "StageViewportTarget + TonemapCameraModifier") },

    { 2, "SubViewport", "NestedStagePortal", StudioNodeCategory::CoreSystem, ChromaConstructKind::Stage, "37:27",
      "Nests directly inside any 3D surface or UI panel without requiring a separate SubViewportContainer wrapper",
      "Zero-copy bindless descriptor slot (`u_atlas`) with asynchronous compute composition",
      detail_preset::make_sys(CatalogPrimKind::Cube, { 0.30F, 0.85F, 0.95F }, NodeSpawnFlags::kChromaUiBinding, "NestedStagePortal + BindlessRenderTarget") },

    { 3, "Window", "NativeOsWindowVessel", StudioNodeCategory::CoreSystem, ChromaConstructKind::Stage, "37:41",
      "Unified multi-DPI borderless/dockable window with instant fullscreen and monitor-snap controls",
      "Per-monitor Win32/Wayland/Cocoa swapchain + DWM frame-fence pacing",
      detail_preset::make_sys(CatalogPrimKind::Cube, { 0.42F, 0.70F, 0.92F }, NodeSpawnFlags::kChromaUiBinding, "NativeOsWindowVessel + SwapchainFence") },

    { 4, "AcceptDialog", "UiModalConfirmCapsule", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "37:56",
      "Folds inline onto any UI/Scene Element or triggers via 1-line C++23 callback without scene clutter",
      "GPU frosted-glass backdrop blur (`kBlur` variant 5) + automatic focus-ring trap",
      detail_preset::make_ui({ 0.38F, 0.78F, 0.95F }, "UiModalConfirmCapsule + BackdropBlur") },

    { 5, "ConfirmationDialog", "UiDualChoiceModalCapsule", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "37:56",
      "Pre-wired Confirm/Cancel/Destructive action capsules with Enter/Escape hotkeys out of the box",
      "Type-safe `Pulse<bool>` completion signal + spring-damped scale-in animation",
      detail_preset::make_ui({ 0.40F, 0.82F, 0.72F }, "UiDualChoiceModalCapsule + Pulse<bool>") },

    { 6, "FileDialog", "VfsAssetPickerModal", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "38:09",
      "Integrated live mesh/texture preview + drag-and-drop direct assignment + breadcrumb folder tree",
      "Asynchronous `std::filesystem` + `.cdpak` virtual file system indexer with zero UI stall",
      detail_preset::make_ui({ 0.92F, 0.76F, 0.32F }, "VfsAssetPickerModal + AsyncDirScanner") },

    { 7, "Popup", "UiFloatingOverlayElement", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "38:19",
      "Auto-clamps to screen bounds `[6, DisplaySize - 6]` and auto-closes on outside click or ESC",
      "Z-ordered popup draw-list layer with hardware scissor intersection stack",
      detail_preset::make_ui({ 0.52F, 0.72F, 0.96F }, "UiFloatingOverlayElement + AutoScreenClamp") },

    { 8, "PopupMenu", "UiContextCommandMenu", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "38:30",
      "Supports nested submenus that automatically cascade vertically and align to parent item bounds",
      "Single-draw-call batched SDF menu frame + keyboard shortcut chord dispatcher",
      detail_preset::make_ui({ 0.48F, 0.68F, 0.94F }, "UiContextCommandMenu + CascadingSubmenus") },

    { 9, "PopupPanel", "UiFrostedPopoverPanel", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "38:36",
      "Hosts arbitrary nested ChromaUI widgets (sliders, color pickers, trees) with zero layout boilerplate",
      "Hardware elevation drop-shadow (`kShadow` variant 6) + frosted glass shader pass",
      detail_preset::make_ui({ 0.58F, 0.64F, 0.92F }, "UiFrostedPopoverPanel + ElevationShadow") },

    { 10, "CanvasItem", "PlanarDrawElement", StudioNodeCategory::CoreSystem, ChromaConstructKind::Element, "38:44",
      "Unified 2D + UI vector drawing base with built-in clip-rect protection and material variants",
      "65,532-vertex `DrawBatcher` + consecutive scissor/descriptor draw-call coalescing",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.45F, 0.88F, 0.78F }, NodeSpawnFlags::kBoxCollider, "PlanarDrawElement + DrawBatcher") },

    { 11, "CanvasLayer", "HudStageLayer", StudioNodeCategory::CoreSystem, ChromaConstructKind::Stage, "38:58",
      "Combines 2D screen-space HUD and 3D world-space projected tags (`DrawWorldSpaceEntityTag`) in one layer",
      "Resolution-independent DPI scaling + de-overlapped screen-space bounding solver",
      detail_preset::make_ui({ 0.32F, 0.92F, 0.64F }, "HudStageLayer + WorldSpaceProjector") },

    { 12, "AudioStreamPlayer", "DspSoundEmitterCapsule", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "39:18",
      "Folds inline onto any Element with built-in ADSR envelope and procedural synth fallback",
      "Lock-free ring-buffer PCM mixer + biquad filter & parametric EQ DSP chain",
      detail_preset::make_sys(CatalogPrimKind::Sphere, { 0.95F, 0.68F, 0.25F }, 0u, "DspSoundEmitterCapsule + LockFreePcmMixer") },

    { 13, "AnimationPlayer", "TimelinePulseSequencer", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "39:27",
      "Animates any reflected C++23 property, material parameter, or transform with interactive curve scrubbing",
      "SIMD cubic Bezier keyframe evaluator + deterministic fixed-tick event triggers",
      detail_preset::make_sys(CatalogPrimKind::Cylinder, { 0.88F, 0.45F, 0.92F }, NodeSpawnFlags::kKinematicMover, "TimelinePulseSequencer + KinematicMover") },

    { 14, "AnimationTree", "InertialAnimBlendTree", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "39:38",
      "Visual state machine + 1D/2D blend spaces with automatic velocity-driven stride speed scaling",
      "Quaternion dual-number skinning + inertialization transition blending",
      detail_preset::make_3d(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 1.0F, 1.0F, 1.0F }, 0.0F, 0.6F, 0.0F, 1,
                             NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController, "Characters", "InertialAnimBlendTree + SkinnedAvatar") },

    { 15, "HTTPRequest", "AsyncRestFetchCapsule", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "39:47",
      "Folds inline with automatic JSON parsing (`cd::asset::json`) and retry/timeout policies",
      "Coroutine-ready thread-pool task dispatch with zero main-thread frame hitches",
      detail_preset::make_sys(CatalogPrimKind::Sphere, { 0.28F, 0.82F, 0.96F }, 0u, "AsyncRestFetchCapsule + JsonParser") },

    { 16, "MultiplayerSpawner", "NetAuthorityVesselSpawner", StudioNodeCategory::CoreSystem, ChromaConstructKind::Stage, "39:55",
      "Auto-replicates any spawned Vessel/Element across peers without manual spawn list registration",
      "Deterministic entity archetype ID hashing + interest-area spatial bitmask filtering",
      detail_preset::make_sys(CatalogPrimKind::Cone, { 0.32F, 0.94F, 0.58F }, 0u, "NetAuthorityVesselSpawner + SpatialInterest") },

    { 17, "MultiplayerSynchronizer", "NetDeltaStateSynchronizer", StudioNodeCategory::CoreSystem, ChromaConstructKind::PulseBus, "39:55",
      "Zero-config delta compression and Hermite snapshot interpolation folded directly into any Vessel",
      "Quantized bit-packed transform/velocity ring buffer + client-side prediction reconciliation",
      detail_preset::make_sys(CatalogPrimKind::Torus, { 0.25F, 0.90F, 0.85F }, NodeSpawnFlags::kKinematicMover, "NetDeltaStateSynchronizer + SnapshotRing") },

    { 18, "ResourcePreloader", "AsyncVfsPakStreamer", StudioNodeCategory::CoreSystem, ChromaConstructKind::TraitCapsule, "40:10",
      "Pre-warms meshes, glTFs, KTX2 textures, and SPIR-V shaders in the background with live progress bar",
      "LRU budget-enforced texture/mesh staging queue + dedicated Vulkan transfer queue upload",
      detail_preset::make_sys(CatalogPrimKind::Cube, { 0.92F, 0.80F, 0.30F }, 0u, "AsyncVfsPakStreamer + LruStagingQueue") },

    { 19, "ShaderGlobalsOverride", "GluonGlobalUniformBus", StudioNodeCategory::CoreSystem, ChromaConstructKind::PulseBus, "40:25",
      "Live inspector sliders for global wind, time, atmosphere, and BRDF parameters with instant hot-reload",
      "`cd::gluon` shared UBO/PushConstant layout + hitch-free `IncludeDependencyTracker`",
      detail_preset::make_sys(CatalogPrimKind::Sphere, { 0.90F, 0.42F, 0.78F }, NodeSpawnFlags::kEnvBloomLens, "GluonGlobalUniformBus + ShaderHotReload") },

    { 20, "Timer", "DeterministicPulseTimer", StudioNodeCategory::CoreSystem, ChromaConstructKind::PulseBus, "41:00",
      "Can be folded inline on any Element or used as a repeatable/one-shot cooldown with visual progress",
      "Fixed-timestep accumulator (`on_fixed_tick`) immune to variable frame-rate drift",
      detail_preset::make_sys(CatalogPrimKind::Cylinder, { 0.98F, 0.72F, 0.28F }, 0u, "DeterministicPulseTimer + FixedTickAccum") },

    // =========================================================================
    // 2. PLANAR 2D NODES (50 Nodes, IDs 21..70)
    // =========================================================================
    { 21, "Node2D", "Planar2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "02:29",
      "Supports nested 2D transforms AND seamless embedding inside 3D stages as billboard/planar layers",
      "SoA SIMD 2D matrix propagation + Z-index hardware depth sorting",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.35F, 0.82F, 0.98F }, NodeSpawnFlags::kBoxCollider, "Planar2DElement + SoATransform2D") },

    { 22, "Camera2D", "PlanarLens2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "02:39",
      "Built-in deadzone smoothing, screen-shake trauma decay, and pixel-perfect integer zoom snapping",
      "Critically damped spring look-ahead solver + frustum culling bounds export",
      detail_preset::make_2d(CatalogPrimKind::Cone, { 0.96F, 0.82F, 0.28F }, NodeSpawnFlags::kBoxCollider, "PlanarLens2D + SpringDeadzone") },

    { 23, "Sprite2D", "QuadSprite2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "02:52",
      "Drag-and-drop disk PNG/JPG/KTX2 assignment with automatic pivot, atlas trim, and normal-map lighting",
      "Bindless texture array indexing (`slot 1..250`) + instanced quad batching",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.95F, 0.95F, 0.98F }, NodeSpawnFlags::kBoxCollider, "QuadSprite2DElement + BindlessAtlas", 2) },

    { 24, "AnimatedSprite2D", "FlipbookSprite2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "02:52",
      "Auto-slices sprite sheets by grid or JSON metadata without manual frame-by-frame setup",
      "GPU UV-offset flipbook evaluation + per-frame gameplay event pulse triggers",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.98F, 0.72F, 0.32F }, NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover, "FlipbookSprite2DElement + UvFlipbook", 2) },

    { 25, "CollisionObject2D", "PlanarCollisionElement2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "03:07",
      "Includes built-in collision shape capsule by default — never shows Godot's missing-shape warning!",
      "Broadphase spatial hash grid + bitmask layer/mask collision filtering",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.30F, 0.92F, 0.58F }, NodeSpawnFlags::kBoxCollider, "PlanarCollisionElement2D + SpatialHash") },

    { 26, "PhysicsBody2D", "PlanarPhysicsVessel2D", StudioNodeCategory::Node2D, ChromaConstructKind::Vessel, "03:17",
      "Unified 2D physics vessel that switches between Static, Kinematic, and Dynamic in 1 dropdown",
      "Sub-stepped impulse solver with Coulomb friction and restitutionmanifolds",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.92F, 0.56F, 0.24F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider, "PlanarPhysicsVessel2D + RigidBody") },

    { 27, "CollisionShape2D", "PlanarHullCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "03:25",
      "Folds inline into its parent Vessel or nests as a multi-part child shape with live green/red wireframe",
      "Analytical AABB / Circle / Capsule SAT & GJK-EPA penetration depth solver",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.25F, 0.95F, 0.48F }, NodeSpawnFlags::kBoxCollider, "PlanarHullCapsule2D + LiveWireframe") },

    { 28, "CollisionPolygon2D", "PlanarConcavePolyCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "03:25",
      "Auto-generates convex decomposition from any sprite alpha mask or user polygon in 1 click",
      "Hertel-Mehlhorn convex partitioning + BVH leaf acceleration",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.32F, 0.90F, 0.54F }, NodeSpawnFlags::kBoxCollider, "PlanarConcavePolyCapsule2D + ConvexDecomp") },

    { 29, "StaticBody2D", "PlanarAnchorBody2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "03:37",
      "Spawns with pre-attached BoxCollider and surface friction/bounce settings ready immediately",
      "Zero-CPU-overhead static BVH leaf with instant edit-mode transform dirty rebuild",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.50F, 0.55F, 0.62F }, NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "PlanarAnchorBody2D + StaticCollider", 3) },

    { 30, "AnimatableBody2D", "PlanarKinematicMover2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "03:53",
      "Built-in oscillating/rotating platform parameters (`amplitude`, `frequency_hz`, `carry_riders`)",
      "Velocity-derived rider transport solver that moves standing characters without jitter",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.28F, 0.78F, 0.96F }, NodeSpawnFlags::kRigidBodyKinematic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover, "PlanarKinematicMover2D + RiderCarry", 4) },

    { 31, "RigidBody2D", "PlanarKineticBody2D", StudioNodeCategory::Node2D, ChromaConstructKind::Vessel, "04:08",
      "Pre-bundled with mass, gravity, bounce restitution, and one-click Impulse test buttons in Inspector",
      "Continuous Collision Detection (CCD) ray-sweep + warm-started sequential impulses",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.94F, 0.44F, 0.22F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider, "PlanarKineticBody2D + DynamicCCD", 2) },

    { 32, "CharacterBody2D", "PlanarAvatarVessel2D", StudioNodeCategory::Node2D, ChromaConstructKind::Vessel, "04:19",
      "Includes Coyote-Time, Jump-Buffer, Slope-Snap, and Moving-Platform carry out of the box",
      "Collide-and-slide capsule sweep solver + `HumanPilot` / `SynthPilot` dual compatibility",
      detail_preset::make_2d(CatalogPrimKind::GltfAvatar, { 0.35F, 0.94F, 0.55F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController, "PlanarAvatarVessel2D + CoyoteJump") },

    { 33, "Joint2D", "PlanarXpbdConstraint2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "04:31",
      "Folds inline onto connected bodies with live constraint force visualization",
      "Unconditionally stable XPBD compliance solver even at high mass ratios (1000:1)",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.88F, 0.72F, 0.28F }, NodeSpawnFlags::kBoxCollider, "PlanarXpbdConstraint2D + ComplianceSolver") },

    { 34, "DampedSpringJoint2D", "PlanarSpringDamperCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "04:41",
      "Direct frequency (Hz) and damping ratio (0..1) controls instead of raw stiffness guesswork",
      "Implicit Euler analytical spring-damper integration (zero explosion under large dt)",
      detail_preset::make_2d(CatalogPrimKind::Torus, { 0.28F, 0.92F, 0.75F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider, "PlanarSpringDamperCapsule2D + ImplicitSpring") },

    { 35, "GrooveJoint2D", "PlanarPrismaticTrackCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "04:51",
      "Includes optional linear motor speed, force limits, and soft end-stop bumpers",
      "Projected Gauss-Seidel linear axis constraint with motor impulse clamping",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.76F, 0.68F, 0.94F }, NodeSpawnFlags::kKinematicMover | NodeSpawnFlags::kBoxCollider, "PlanarPrismaticTrackCapsule2D + LinearMotor") },

    { 36, "PinJoint2D", "PlanarRevolutePinCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "05:02",
      "Supports angular limits, torque motor, and breakable force threshold in a single capsule",
      "XPBD positional + angular hinge projection with zero positional drift",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.92F, 0.58F, 0.36F }, NodeSpawnFlags::kBoxCollider, "PlanarRevolutePinCapsule2D + AngularLimits") },

    { 37, "Area2D", "PlanarSensorZone2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "05:10",
      "1-dropdown presets for Collectible (+Score), JumpPad (+Boost), Hazard (-HP), Heal, or Goal Beacon",
      "Zero-allocation overlap manifold + local gravity/damping override field",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 1.00F, 0.84F, 0.22F }, NodeSpawnFlags::kTriggerCollectible | NodeSpawnFlags::kSphereCollider, "PlanarSensorZone2D + TriggerVolume") },

    { 38, "AudioListener2D", "PlanarAudioEar2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "05:29",
      "Auto-attaches to active `PlanarLens2D` or `PlanarAvatarVessel2D` with smooth stereo panning",
      "Distance-attenuated equal-power panner + Doppler pitch shift calculator",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.42F, 0.88F, 0.96F }, NodeSpawnFlags::kSphereCollider, "PlanarAudioEar2D + EqualPowerPan") },

    { 39, "AudioStreamPlayer2D", "PlanarSoundSource2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "05:39",
      "Visualizes inner/outer audible radius rings directly in the viewport",
      "2D wall-occlusion raycast low-pass filter + voice-stealing priority virtualizer",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.96F, 0.64F, 0.24F }, NodeSpawnFlags::kSphereCollider, "PlanarSoundSource2D + OcclusionLPF") },

    { 40, "CPUParticles2D", "SimdPlanarSwarm2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "05:49",
      "1-click presets for sparks, smoke, rain, and footsteps with deterministic seed replay",
      "AVX2/NEON SoA particle integrator with post-expire compaction (`Wave 1.1` fix)",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 1.00F, 0.62F, 0.20F }, NodeSpawnFlags::kTriggerCollectible, "SimdPlanarSwarm2D + SoAIntegrator") },

    { 41, "GPUParticles2D", "ComputeSparkSwarm2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "05:49",
      "Supports 2D SDF collision bounce and attraction fields without writing custom shaders",
      "Vulkan compute ring-buffer emitter + indirect quad draw (`cd::gpu_particles`)",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 1.00F, 0.78F, 0.28F }, NodeSpawnFlags::kLightPoint, "ComputeSparkSwarm2D + IndirectCompute") },

    { 42, "TileMap", "TileCanvas2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "06:08",
      "Unified multi-layer tilemap ( merges Godot's TileMap + TileMapLayer ) with WFC auto-tiling",
      "Chunked GPU instanced quad batches + merged greedy-mesh collision strips",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.36F, 0.82F, 0.64F }, NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "TileCanvas2DElement + GreedyCollider", 2) },

    { 43, "CanvasModulate", "PlanarRadianceTint2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "06:29",
      "Includes Kelvin color-temperature slider (1800K..15000K) and day-night cycle curve",
      "Linear-space HDR scene multiplier before ACES/AgX tonemapping",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.85F, 0.75F, 0.98F }, NodeSpawnFlags::kEnvTonemap, "PlanarRadianceTint2D + KelvinTint") },

    { 44, "Light2D", "PlanarLightBase2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "06:39",
      "Physical Kelvin color + normal-map height specular response on 2D sprites",
      "Clustered 2D light tile culling + 1D polar ray-marched soft shadows",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 1.00F, 0.90F, 0.55F }, NodeSpawnFlags::kLightPoint, "PlanarLightBase2D + NormalSpecular") },

    { 45, "PointLight2D", "RadialLight2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "06:46",
      "Spawns a live radial light with interactive radius handle and flicker/pulse options",
      "Signed-distance-field penumbra soft shadow evaluation in a single pass",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 1.00F, 0.85F, 0.38F }, NodeSpawnFlags::kLightPoint, "RadialLight2DElement + SdfSoftShadow") },

    { 46, "DirectionalLight2D", "SolarLight2DElement", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "07:10",
      "Couples sun angle, Kelvin warmth, and 2D elevation shadow length in one slider",
      "Infinite-distance parallel ray shadow sweep across 2D occluder SDF",
      detail_preset::make_2d(CatalogPrimKind::Cone, { 1.00F, 0.92F, 0.62F }, NodeSpawnFlags::kLightDirectional, "SolarLight2DElement + SunElevation") },

    { 47, "LightOccluder2D", "SdfShadowOccluder2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "07:23",
      "Auto-extracts occluder contour from sprite silhouette without manual polygon tracing",
      "Jump-Flooding Algorithm (JFA) real-time 2D Signed Distance Field generator",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.28F, 0.32F, 0.40F }, NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "SdfShadowOccluder2D + JumpFloodSDF", 4) },

    { 48, "Line2D", "GpuRibbonStroke2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "07:33",
      "Supports pressure width curves, miter/round joins, dashed patterns, and glowing trails",
      "Analytical anti-aliased cubic Bezier & polyline tessellator (`AddBezierCubic`)",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.28F, 0.90F, 0.98F }, 0u, "GpuRibbonStroke2D + AntiAliasedBezier") },

    { 49, "Marker2D", "PlanarBeacon2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "07:47",
      "Displays custom color-coded crosshair/label in editor and acts as a spawn/waypoint target",
      "Zero-memory-overhead transform tag in cooked runtime builds",
      detail_preset::make_2d(CatalogPrimKind::Cone, { 0.35F, 0.96F, 0.65F }, 0u, "PlanarBeacon2D + SpawnTarget") },

    { 50, "MeshInstance2D", "PlanarSurfaceMesh2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "07:56",
      "Renders any 2D/3D mesh with full PBR/iridescent shading in 2D space",
      "Hardware indexed triangle pipeline with UV deformation weights",
      detail_preset::make_2d(CatalogPrimKind::Torus, { 0.85F, 0.52F, 0.95F }, NodeSpawnFlags::kBoxCollider, "PlanarSurfaceMesh2D + IndexedMesh") },

    { 51, "MultiMeshInstance2D", "PlanarIndirectSwarmMesh2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "08:05",
      "Populates 50,000+ 2D foliage/bullet instances with brush painting or procedural scatter",
      "GPU SSBO instance transform buffer + compute frustum culling",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.42F, 0.92F, 0.48F }, 0u, "PlanarIndirectSwarmMesh2D + SsboInstancing") },

    { 52, "NavigationRegion2D", "PlanarNavField2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "08:15",
      "Auto-bakes navigable polygons from `PlanarAnchorBody2D` and `TileCanvas2DElement` colliders",
      "Hierarchical A* + Flow-Field vector grid for 10,000 simultaneous agents",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.24F, 0.76F, 0.92F }, NodeSpawnFlags::kBoxCollider, "PlanarNavField2D + FlowFieldAStar") },

    { 53, "NavigationLink2D", "PlanarNavJumpPortal2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "08:15",
      "Visualizes bidirectional jump/drop/teleporter arcs between ledges in the editor",
      "Cost-weighted off-mesh graph edge with parabolic jump trajectory solver",
      detail_preset::make_2d(CatalogPrimKind::Torus, { 0.30F, 0.95F, 0.88F }, NodeSpawnFlags::kTriggerJumpPad, "PlanarNavJumpPortal2D + JumpArc") },

    { 54, "NavigationObstacle2D", "PlanarRvoAvoidanceHull2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "08:15",
      "Folds inline onto any moving crate or hazard so AI agents steer around it automatically",
      "ORCA (Optimal Reciprocal Collision Avoidance) velocity-obstacle half-plane solver",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.95F, 0.52F, 0.28F }, NodeSpawnFlags::kBoxCollider, "PlanarRvoAvoidanceHull2D + ORCA") },

    { 55, "NavigationAgent2D", "PlanarWayfinderPilot2D", StudioNodeCategory::Node2D, ChromaConstructKind::Pilot, "08:15",
      "Includes built-in patrol waypoints and player pursuit (`PatrolWaypointModifier`) without scripting",
      "String-pulled funnel path corridor + ORCA crowd separation",
      detail_preset::make_2d(CatalogPrimKind::Cone, { 0.92F, 0.35F, 0.88F }, NodeSpawnFlags::kPatrolAi | NodeSpawnFlags::kBoxCollider, "PlanarWayfinderPilot2D + PatrolAI") },

    { 56, "ParallaxBackground", "PlanarDepthStage2D", StudioNodeCategory::Node2D, ChromaConstructKind::Stage, "08:35",
      "Previews parallax motion directly in the editor viewport while panning the camera",
      "Single-pass multi-layer UV depth-scroll shader with atmospheric haze",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.32F, 0.52F, 0.82F }, 0u, "PlanarDepthStage2D + MultiLayerScroll", 1) },

    { 57, "ParallaxLayer", "PlanarParallaxStrata2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "08:35",
      "Seamless infinite horizontal/vertical mirroring + auto-wind drift speed",
      "Hardware repeat-sampler wrap with sub-pixel jitter elimination",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.42F, 0.64F, 0.90F }, NodeSpawnFlags::kKinematicMover, "PlanarParallaxStrata2D + InfiniteWrap", 1) },

    { 58, "Path2D", "PlanarBezierSpline2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "08:47",
      "Interactive Bezier control handles with arc-length parameterization (constant speed!)",
      "Gauss-Legendre arc-length lookup table for zero-stretching velocity",
      detail_preset::make_2d(CatalogPrimKind::Torus, { 0.95F, 0.78F, 0.25F }, NodeSpawnFlags::kKinematicMover, "PlanarBezierSpline2D + ArcLengthLUT") },

    { 59, "PathFollow2D", "PlanarSplineFollowerCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "08:47",
      "Folds directly onto any platform or enemy without needing an intermediate PathFollow node",
      "Analytical tangent/normal Frenet frame evaluation + ping-pong/loop modes",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.35F, 0.88F, 0.95F }, NodeSpawnFlags::kKinematicMover | NodeSpawnFlags::kBoxCollider, "PlanarSplineFollowerCapsule2D + FrenetMover", 4) },

    { 60, "Polygon2D", "PlanarDelaunayPolygon2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "08:58",
      "Supports holes, skeletal bone weights, and automatic UV unwrapping",
      "Constrained Delaunay triangulation + GPU skinned vertex deformer",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.88F, 0.48F, 0.68F }, NodeSpawnFlags::kBoxCollider, "PlanarDelaunayPolygon2D + DelaunayMesh", 5) },

    { 61, "RayCast2D", "PlanarRayProbeCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "09:07",
      "Folds inline onto any entity and draws green/red hit normal markers in the viewport",
      "SIMD BVH ray-slab intersection with multi-hit & layer mask query",
      detail_preset::make_2d(CatalogPrimKind::Cone, { 0.25F, 0.98F, 0.55F }, NodeSpawnFlags::kBoxCollider, "PlanarRayProbeCapsule2D + SimdRayCast") },

    { 62, "ShapeCast2D", "PlanarSweepProbeCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "09:16",
      "Thick circle/box/capsule sweep for ledge detection and wide laser beams",
      "Conservative advancement GJK time-of-impact continuous sweep",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.30F, 0.94F, 0.68F }, NodeSpawnFlags::kCapsuleCollider, "PlanarSweepProbeCapsule2D + GjkSweep") },

    { 63, "RemoteTransform2D", "PlanarTransformLinkCapsule2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "09:28",
      "Supports weighted blending, axis locks, and spring lag between any two Elements",
      "DAG-ordered transform constraint evaluation (zero 1-frame lag)",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.68F, 0.82F, 0.98F }, 0u, "PlanarTransformLinkCapsule2D + ZeroLagSync") },

    { 64, "Skeleton2D", "PlanarBoneRig2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "09:42",
      "Includes built-in 2-bone & FABRIK IK solver and spring-bone secondary motion",
      "Flat cache-aligned bone matrix palette uploaded via dynamic SSBO",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.92F, 0.86F, 0.72F }, NodeSpawnFlags::kCapsuleCollider, "PlanarBoneRig2D + FabrikIK2D") },

    { 65, "Bone2D", "PlanarJointNode2D", StudioNodeCategory::Node2D, ChromaConstructKind::Element, "09:42",
      "Auto-computes rest length and angle limits when nested under `PlanarBoneRig2D`",
      "Local-to-skeleton SIMD transform concatenation",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.88F, 0.80F, 0.64F }, 0u, "PlanarJointNode2D + AngleLimits") },

    { 66, "PhysicalBone2D", "PlanarRagdollBone2D", StudioNodeCategory::Node2D, ChromaConstructKind::Vessel, "09:42",
      "Smoothly blends between keyframe animation and active physics ragdoll (`0..1` weight)",
      "PD-controller joint torque drive + XPBD angle limit projection",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.95F, 0.64F, 0.42F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider, "PlanarRagdollBone2D + ActiveRagdoll") },

    { 67, "VisibleOnScreenNotifier2D", "PlanarFrustumSensor2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "10:01",
      "Folds inline into any Element and emits enter/exit screen pulses with configurable margin",
      "SIMD 4-plane 2D view-rect overlap test in < 2 nanoseconds per entity",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.55F, 0.92F, 0.78F }, 0u, "PlanarFrustumSensor2D + SimdCullTest") },

    { 68, "VisibleOnScreenEnabler2D", "PlanarFrustumSleepGate2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "10:01",
      "Automatically pauses off-screen AI/particles/animations while preserving state",
      "Archetype chunk enable-bit toggling (zero structural ECS move cost)",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.48F, 0.88F, 0.72F }, 0u, "PlanarFrustumSleepGate2D + ChunkBitGate") },

    { 69, "CanvasGroup", "PlanarCompositeLayer2D", StudioNodeCategory::Node2D, ChromaConstructKind::Stage, "10:17",
      "Applies group-level alpha, outline, or drop-shadow to nested child sprites without alpha-overlap artifacts",
      "Transient offscreen RHI tile render pass with SDF outline dilation",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 0.64F, 0.78F, 0.96F }, 0u, "PlanarCompositeLayer2D + OffscreenComposite") },

    { 70, "BackBufferCopy", "PlanarFrameHistoryTap2D", StudioNodeCategory::Node2D, ChromaConstructKind::TraitCapsule, "10:27",
      "Zero-setup screen refraction (water ripples, shockwaves, frosted glass) for any 2D/UI element",
      "Mip-chained scene color history pyramid (`u_scene_color_mip`) sampling",
      detail_preset::make_2d(CatalogPrimKind::Sphere, { 0.72F, 0.90F, 1.00F }, NodeSpawnFlags::kEnvBloomLens, "PlanarFrameHistoryTap2D + MipRefraction") },

    // =========================================================================
    // 3. SPATIAL 3D NODES (88 Nodes, IDs 71..158)
    // =========================================================================
    { 71, "Node3D", "Spatial3DElement", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "10:43",
      "Inspector shows intuitive Euler Degrees (Pitch/Yaw/Roll) + 1-click Ground Snap (`G`) + Uniform Scale Lock",
      "Quaternion + TRS cache-aligned `cd::scene::Scene` hierarchy with dirty-gated TLAS hash",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.82F, 0.85F, 0.90F }, 0.20F, 0.35F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Default", "Spatial3DElement + EulerGizmo + GroundSnap") },

    { 72, "Marker3D", "SpawnBeacon3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "10:59",
      "Acts as a smart spawn point, camera bookmark target, or AI waypoint with 3D viewport gizmo",
      "Zero-draw-call runtime transform marker indexed by `StageDirector`",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.55F, 0.85F, 0.55F }, { 0.25F, 0.95F, 0.65F }, 0.40F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial, "Gameplay", "SpawnBeacon3D + StageDirectorTarget") },

    { 73, "VisualInstance3D", "VisualRadianceElement3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "11:09",
      "Unified visibility layer mask, fade distance range, and RT shadow/reflection participation toggles",
      "Hardware BLAS instance mask + Hi-Z screen-space bounding-sphere culler",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.95F, 0.95F, 0.95F }, { 0.78F, 0.84F, 0.95F }, 0.65F, 0.22F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Geometry", "VisualRadianceElement3D + BlasInstance") },

    { 74, "GeometryInstance3D", "RenderableSurfaceElement3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "11:26",
      "Supports drag-and-drop `.mesh`/`.gltf`/`.obj` AND `.png`/`.jpg`/`.tex` directly from Content Browser",
      "Per-instance push-constant + SSBO material table + `VK_KHR_ray_query` TLAS inclusion",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.0F, 1.0F, 1.0F }, { 0.92F, 0.88F, 0.80F }, 0.50F, 0.24F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Geometry", "RenderableSurfaceElement3D + BindlessTex") },

    { 75, "MeshInstance3D", "SurfaceMesh3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "11:45",
      "1-click primitive/glTF/OBJ switcher + built-in Turquin Multi-Scatter PBR & Iridescence material",
      "Cook-Torrance GGX + Turquin (2019) energy compensation + hardware BLAS ray tracing",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.0F, 1.0F, 1.0F }, { 1.00F, 0.78F, 0.34F }, 1.00F, 0.16F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Geometry", "SurfaceMesh3D + TurquinPBR + BLAS") },

    { 76, "MultiMeshInstance3D", "SwarmMesh3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "11:58",
      "Includes procedural grid/ring/surface scatter generator in Inspector — no scripting required to populate!",
      "GPU `vkCmdDrawIndexedIndirect` + SSBO instance matrix buffer (`inst_mat_ssbo`)",
      detail_preset::make_3d(CatalogPrimKind::TorusKnot, { 1.1F, 1.1F, 1.1F }, { 0.30F, 0.92F, 0.62F }, 0.85F, 0.18F, 380.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Geometry", "SwarmMesh3D + IndirectInstancing") },

    { 77, "ImporterMeshInstance3D", "CookedAssetMesh3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "22:01",
      "Automatically normalizes bounds, generates tangents/normals, and builds physics hull + BLAS on drop",
      "`cd::studio::parse_wavefront_obj_mesh` + `cd::asset::gltf::load_gltf` + auto-BLAS upload",
      detail_preset::make_3d(CatalogPrimKind::TorusKnot, { 1.0F, 1.0F, 1.0F }, { 0.95F, 0.82F, 0.45F }, 0.75F, 0.20F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Geometry", "CookedAssetMesh3D + AutoNormalizeBLAS") },

    { 78, "Label3D", "WorldMsdfLabel3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "12:16",
      "Crisp at any zoom distance, supports camera-facing billboarding and automatic screen-space de-overlapping",
      "Multi-channel Signed Distance Field (MSDF) font atlas + `DrawWorldSpaceEntityTag`",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.2F, 0.45F, 0.12F }, { 0.35F, 0.92F, 1.00F }, 0.10F, 0.25F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kChromaUiBinding, "UI", "WorldMsdfLabel3D + MsdfBillboard") },

    { 79, "Decal", "ProjectionDecal3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "12:30",
      "Projects albedo, normal, roughness, and emissive splatters onto curved geometry with angle fade",
      "Oriented Bounding Box (OBB) deferred G-buffer / depth-reconstructed projection (`cd::decal`)",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.4F, 0.25F, 1.4F }, { 0.95F, 0.38F, 0.22F }, 0.15F, 0.30F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Geometry", "ProjectionDecal3D + ObbDepthProject") },

    { 80, "SpriteBase3D", "BillboardQuadBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "41:31",
      "Unified Y-axis or full spherical billboarding with depth-tested shadow casting",
      "Camera-aligned vertex shader basis + alpha-to-coverage hardware MSAA",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 0.12F }, { 0.90F, 0.92F, 0.96F }, 0.10F, 0.35F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Geometry", "BillboardQuadBase3D + SphericalBasis") },

    { 81, "Sprite3D", "BillboardSprite3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "12:40",
      "Receives full 3D clustered lighting, CSM shadows, and volumetric fog out of the box",
      "Bindless texture array sampling (`cd_albedo_tex`) in full HDR PBR pass",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.1F, 1.1F, 0.14F }, { 1.0F, 1.0F, 1.0F }, 0.08F, 0.42F, 0.0F, 1,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Geometry", "BillboardSprite3D + LitHdrPBR") },

    { 82, "AnimatedSprite3D", "BillboardFlipbook3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "12:40",
      "Plays sprite-sheet animations in 3D space with velocity-synced playback rate",
      "Instanced UV sub-rect transform + motion-vector velocity output for TAA",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.1F, 1.1F, 0.14F }, { 0.98F, 0.76F, 0.32F }, 0.15F, 0.30F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover, "Geometry", "BillboardFlipbook3D + MotionVectors") },

    { 83, "WorldEnvironment", "OpticsAndAetherStage3D", StudioNodeCategory::Node3D, ChromaConstructKind::Stage, "13:01",
      "Modular C++23 Environment Stack (`SkyAtmosphere`, `Fog`, `BloomLens`, `TonemapCamera`) — pay only for active passes!",
      "Bruneton sky + Wronski froxel fog + 7-pass HDR bloom + ACES/Hable/AgX + Ray-Traced DDGI",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.2F, 1.2F, 1.2F }, { 0.38F, 0.78F, 1.00F }, 0.95F, 0.08F, 480.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvSkyDdgi | NodeSpawnFlags::kEnvBloomLens | NodeSpawnFlags::kEnvTonemap,
                             "Default", "OpticsAndAetherStage3D + Sky + Bloom + AgX") },

    { 84, "FogVolume", "FogPocket3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:19",
      "Can be used globally OR placed as a local OBB/Sphere fog pocket with god-ray light shafts",
      "Temporal froxel volumetric scattering (`cd::volumetric_fog` + `cd::light_shafts`)",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.6F, 1.2F, 1.6F }, { 0.62F, 0.82F, 0.98F }, 0.10F, 0.85F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvFog, "Default", "FogPocket3D + FroxelVolumetricFog") },

    { 85, "GPUParticles3D", "SparkSwarm3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:26",
      "1-click presets for plasma sparks, fire, magic portals, and debris with depth-buffer collision",
      "Compute shader simulation + hardware depth-buffer bounce + HDR bloom emission",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.75F, 0.75F, 0.75F }, { 1.00F, 0.72F, 0.22F }, 1.00F, 0.10F, 460.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kLightPoint | NodeSpawnFlags::kTriggerCollectible,
                             "Physics", "SparkSwarm3D + ComputeParticles + PointLight") },

    { 86, "CPUParticles3D", "DeterministicSimdSwarm3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "41:13",
      "Identical visual controls to `SparkSwarm3D` with bit-exact deterministic replay across all platforms",
      "SIMD 8-wide particle integrator + exact CPU physics world raycast collision",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.70F, 0.70F, 0.70F }, { 0.98F, 0.58F, 0.24F }, 0.85F, 0.18F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Physics", "DeterministicSimdSwarm3D + SimdParticles") },

    { 87, "GPUParticlesAttractor3D", "SwarmForceFieldBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "13:50",
      "Affects both GPU particles AND dynamic rigid bodies within its radius when enabled!",
      "Unified analytical radial/axial force field evaluated in compute & physics substeps",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 1.1F, 1.1F, 1.1F }, { 0.32F, 0.92F, 0.98F }, 0.90F, 0.14F, 420.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover, "Physics", "SwarmForceFieldBase3D + UnifiedForce") },

    { 88, "GPUParticlesAttractorBox3D", "SwarmBoxVortexField3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Oriented box wind tunnel / conveyor force field with 3D directional arrow gizmos",
      "OBB-local directional & swirl impulse tensor",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.4F, 0.8F, 1.4F }, { 0.28F, 0.86F, 0.96F }, 0.75F, 0.20F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kTriggerJumpPad, "Physics", "SwarmBoxVortexField3D + WindTunnel") },

    { 89, "GPUParticlesAttractorSphere3D", "SwarmGravityWell3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Black-hole pull or shockwave repulsor sphere with falloff curve editor",
      "Inverse-square radial attractor + orbital tangential spin term",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.9F, 0.9F, 0.9F }, { 0.58F, 0.32F, 0.98F }, 0.95F, 0.10F, 520.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider | NodeSpawnFlags::kTriggerCollectible,
                             "Physics", "SwarmGravityWell3D + RadialAttractor") },

    { 90, "GPUParticlesAttractorVectorField3D", "SwarmCurlNoiseField3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Generates divergence-free 3D curl-noise turbulence procedurally — no external 3D texture file needed!",
      "Analytical 3D Simplex curl-noise compute evaluation + 3D LUT cache",
      detail_preset::make_3d(CatalogPrimKind::TorusKnot, { 0.95F, 0.95F, 0.95F }, { 0.35F, 0.95F, 0.78F }, 0.88F, 0.16F, 380.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover, "Physics", "SwarmCurlNoiseField3D + ProceduralCurl") },

    { 91, "GPUParticlesCollision3D", "SwarmColliderBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "13:50",
      "Folds automatically into any `BoxColliderModifier` or `SphereColliderModifier` — zero duplicate setup",
      "Shared physics-particle broadphase collider descriptor table",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.45F, 0.88F, 0.55F }, 0.30F, 0.35F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "SwarmColliderBase3D + SharedBroadphase") },

    { 92, "GPUParticlesCollisionBox3D", "SwarmObbDeflector3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Deflects both GPU particles and rigid bodies with configurable bounce restitution",
      "OBB signed distance & surface normal analytical deflector",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.5F, 0.4F, 1.5F }, { 0.25F, 0.90F, 0.58F }, 0.40F, 0.28F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Physics", "SwarmObbDeflector3D + StaticOBB") },

    { 93, "GPUParticlesCollisionSphere3D", "SwarmSphereDeflector3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Smooth spherical shield deflector with energy ripple feedback",
      "Analytical sphere reflection vector with Specular/Coulomb restitution",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.1F, 1.1F, 1.1F }, { 0.28F, 0.84F, 0.98F }, 0.90F, 0.12F, 440.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kSphereCollider,
                             "Physics", "SwarmSphereDeflector3D + ShieldSphere") },

    { 94, "GPUParticlesCollisionSDF3D", "SwarmSdfVolumeCollider3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Updates dynamically without waiting for offline SDF baking when objects move",
      "Hardware TLAS ray-query distance probe + 3D clipmap SDF cache",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 1.1F, 1.1F, 1.1F }, { 0.85F, 0.48F, 0.96F }, 0.78F, 0.20F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "SwarmSdfVolumeCollider3D + TlasSdfProbe") },

    { 95, "GPUParticlesCollisionHeightField3D", "SwarmDepthTerrainCollider3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "13:50",
      "Zero-setup collision against terrain and all visible scene geometry using hardware depth",
      "Reversed-Z `D32_SFLOAT` depth-buffer normal reconstruction collision",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.5F, 0.25F, 2.5F }, { 0.42F, 0.68F, 0.38F }, 0.12F, 0.65F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "SwarmDepthTerrainCollider3D + DepthCollide") },

    { 96, "Light3D", "RadianceEmitterBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "14:09",
      "Physical units (Lumens/Lux) + Correlated Color Temperature (`1800K..15000K`) Kelvin slider!",
      "3D Froxel Clustered Light Grid (`cd::cluster`) + hardware ray-query contact shadows",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.45F, 0.45F, 0.45F }, { 1.00F, 0.92F, 0.78F }, 0.10F, 0.10F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kLightPoint, "Lights", "RadianceEmitterBase3D + KelvinCCT") },

    { 97, "OmniLight3D", "RadialLight3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "14:09",
      "Spawns both a visible glowing emitter orb AND a real-time clustered point light selectable in 3D",
      "Windowed inverse-square attenuation + Turquin multi-scatter specular evaluation",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.42F, 0.42F, 0.42F }, { 1.00F, 0.86F, 0.58F }, 0.95F, 0.08F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kLightPoint, "Lights", "RadialLight3D + ClusteredPointLight") },

    { 98, "SpotLight3D", "ConeLight3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "14:42",
      "Interactive inner/outer cone angle gizmo + Kelvin halogen/daylight presets",
      "Smooth dual-cone angular falloff + IES photometric profile multiplier",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.50F, 0.65F, 0.50F }, { 1.00F, 0.94F, 0.76F }, 0.85F, 0.15F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kLightSpot, "Lights", "ConeLight3D + PhotometricSpotLight") },

    { 99, "DirectionalLight3D", "SolarLight3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "14:54",
      "Automatically drives sky atmosphere sun disk, volumetric god-rays, and cascaded shadows together",
      "Cascaded Shadow Maps (CSM) + PCSS soft penumbra + `VK_KHR_ray_query` inline shadow ray",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.65F, 0.65F, 0.65F }, { 1.00F, 0.95F, 0.82F }, 1.00F, 0.05F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kLightDirectional, "Lights", "SolarLight3D + CSM + RayQueryShadows") },

    { 100, "CSGShape3D", "BooleanSolidBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Non-destructive boolean Union/Subtract/Intersect with smooth bevel fillet radius slider!",
      "Analytical Signed Distance Field smooth-min (`smin`) + watertight manifold dual contouring",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.1F, 1.1F, 1.1F }, { 0.88F, 0.90F, 0.94F }, 0.25F, 0.22F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanSolidBase3D + SmoothFilletCSG") },

    { 101, "CSGPrimitive3D", "BooleanPrimitive3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Auto-generates triplanar world-space UVs so brick/marble/wood textures never stretch during scaling",
      "Triplanar derivative UV projection + automatic physics collider synchronization",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.92F, 0.88F, 0.82F }, 0.15F, 0.35F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanPrimitive3D + TriplanarUV") },

    { 102, "CSGBox3D", "BooleanBoxSolid3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "1-click graybox room/wall/platform builder with instant static OBB collider and brick/marble presets",
      "Exact rounded-box SDF (`length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r`) + BLAS",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.6F, 1.0F, 1.2F }, { 0.94F, 0.92F, 0.88F }, 0.12F, 0.28F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanBoxSolid3D + StaticOBB + BrickPBR") },

    { 103, "CSGSphere3D", "BooleanSphereSolid3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Resolution-independent geodesic sphere solid for boolean domes, craters, and arches",
      "Analytical sphere SDF + curvature-adaptive icosahedral tessellation",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.1F, 1.1F, 1.1F }, { 0.95F, 0.96F, 0.98F }, 0.20F, 0.18F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kSphereCollider,
                             "Geometry", "BooleanSphereSolid3D + MarblePBR") },

    { 104, "CSGCylinder3D", "BooleanCylinderSolid3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Supports cylinder, cone, and fluted column profiles with automatic capsule/box collision",
      "Capped-cylinder exact SDF with top/bottom bevel chamfer",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.85F, 1.6F, 0.85F }, { 0.96F, 0.95F, 0.92F }, 0.15F, 0.16F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kCapsuleCollider,
                             "Geometry", "BooleanCylinderSolid3D + ColumnPillar") },

    { 105, "CSGTorus3D", "BooleanTorusSolid3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Parametric inner/outer ring radii + twist knot option for sci-fi portals and pipes",
      "Exact torus & torus-knot SDF + hardware ray-traced BLAS triangle mesh",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 1.2F, 1.2F, 1.2F }, { 0.88F, 0.76F, 0.38F }, 0.92F, 0.16F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanTorusSolid3D + PortalRing") },

    { 106, "CSGPolygon3D", "BooleanExtrudeSweep3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Extrudes or lathe-spins any 2D profile along a `SplineTrack3D` curve with live road/bridge collision",
      "Parallel-transport frame sweep + continuous triangle strip collider",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.4F, 0.35F, 1.4F }, { 0.72F, 0.76F, 0.82F }, 0.35F, 0.30F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanExtrudeSweep3D + SplineSweep") },

    { 107, "CSGMesh3D", "BooleanCustomMesh3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Uses any custom `.obj` or `.gltf` mesh as a boolean cutter or additive brush",
      "BVH winding-number generalized inside/outside test for arbitrary meshes",
      detail_preset::make_3d(CatalogPrimKind::TorusKnot, { 1.0F, 1.0F, 1.0F }, { 0.90F, 0.55F, 0.30F }, 0.65F, 0.24F, 0.0F, 6,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "BooleanCustomMesh3D + WindingNumberCSG") },

    { 108, "CSGCombiner3D", "NestedBooleanAssembly3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:14",
      "Combines nested child CSG elements and bakes to a single static mesh asset in 1 click",
      "Hierarchical CSG DAG evaluation + incremental dirty-subtree remeshing",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.8F, 1.2F, 1.2F }, { 0.92F, 0.94F, 0.98F }, 0.22F, 0.18F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "NestedBooleanAssembly3D + CsgDagCombiner") },

    { 109, "SoftBody3D", "ClothSurface3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "15:38",
      "1-click cloth/jelly/cushion presets with wind response and self-collision — zero vertex pinning pain",
      "Sub-stepped Extended Position-Based Dynamics (XPBD) distance & bending constraints",
      detail_preset::make_3d(CatalogPrimKind::TorusKnot, { 1.05F, 1.05F, 1.05F }, { 0.92F, 0.95F, 0.98F }, 0.25F, 0.10F, 520.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider,
                             "Physics", "ClothSurface3D + XPBDSoftBody + Iridescence") },

    { 110, "CollisionObject3D", "SpatialCollisionElement3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "15:54",
      "Comes pre-equipped with an inline `BoxColliderModifier` so it never errors out with missing child shapes",
      "Live 3D wireframe telemetry (Green = clear, Orange/Red = colliding + contact normal vector)",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.32F, 0.90F, 0.55F }, 0.20F, 0.35F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "SpatialCollisionElement3D + InlineCollider") },

    { 111, "PhysicsBody3D", "PhysicalVesselBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "16:27",
      "Switch between Static, Kinematic, and Dynamic in place without 'Change Type' node replacement!",
      "Unified `RigidBodyModifier` + Jolt-style OBB/Sphere/Capsule narrowphase contact manifold",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.92F, 0.52F, 0.22F }, 0.25F, 0.32F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider,
                             "Physics", "PhysicalVesselBase3D + UnifiedRigidBody") },

    { 112, "CollisionShape3D", "CollisionHullCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "16:38",
      "Folds directly into the Entity's ModifierStack OR nests as a child offset shape with live wireframe",
      "World-space OBB / Sphere / Upright-Capsule narrowphase with penetration depth telemetry",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.28F, 0.94F, 0.48F }, 0.15F, 0.40F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "CollisionHullCapsule3D + ObbNarrowphase") },

    { 113, "CollisionPolygon3D", "ConcavePolygonHullCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "16:38",
      "Extrudes a 3D collision prism or convex hull automatically from mesh vertices",
      "Quickhull 3D convex hull generator + SAT face/edge separating axis test",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 1.0F, 1.0F, 1.0F }, { 0.34F, 0.88F, 0.52F }, 0.20F, 0.36F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "ConcavePolygonHullCapsule3D + Quickhull3D") },

    { 114, "StaticBody3D", "AnchorBody3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "16:52",
      "1-click static wall/floor/ramp with pre-attached `BoxColliderModifier` and surface restitution/friction",
      "Immovable inverse-mass-zero solver body + hardware ray-tracing BLAS obstacle",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.4F, 1.2F, 0.45F }, { 0.52F, 0.56F, 0.64F }, 0.12F, 0.58F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "AnchorBody3D + StaticRigidBody + BoxCollider") },

    { 115, "AnimatableBody3D", "KinematicMoverElement3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "17:16",
      "Built-in sinusoidal translation (`move_axis`, `amplitude`, `frequency_hz`), spin, and `carry_riders`!",
      "Frame-delta rider displacement transfer (`last_frame_delta`) so players ride platforms seamlessly",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.2F, 0.28F, 1.4F }, { 0.25F, 0.78F, 0.96F }, 0.78F, 0.22F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyKinematic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover,
                             "Geometry", "KinematicMoverElement3D + CarryRiders + CarbonPBR") },

    { 116, "RigidBody3D", "KineticBody3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "17:28",
      "Spawns with dynamic mass, gravity, bounce, and live velocity readout + Jump/Push test buttons!",
      "Symplectic Euler + OBB/Sphere/Capsule impulse restitution & Coulomb friction solver",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.0F, 1.0F, 1.0F }, { 0.24F, 0.74F, 0.98F }, 0.88F, 0.12F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider,
                             "Physics", "KineticBody3D + DynamicRigidBody + SphereCollider") },

    { 117, "CharacterBody3D", "AvatarVessel3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "17:44",
      "Auto-bundles Skinned Mesh + Upright CapsuleCollider + WASD/Jump Controller + Spring Camera Follow in 1 click!",
      "World-+Y upright capsule narrowphase + velocity-scaled glTF skeletal animation + plasma blaster",
      detail_preset::make_3d(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 1.0F, 1.0F, 1.0F }, 0.0F, 0.6F, 0.0F, 1,
                             NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController,
                             "Characters", "AvatarVessel3D + CapsuleCollider + CharacterController") },

    { 118, "Area3D", "SensorVolume3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "17:58",
      "Ready-to-use gameplay trigger (Collectible Score, JumpPad Boost, Laser Hazard, Heal, Level Goal) without code!",
      "Capsule-segment-aware proximity & OBB overlap detector (`TriggerVolumeModifier`)",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 1.25F, 1.25F, 1.25F }, { 0.25F, 0.95F, 1.00F }, 0.98F, 0.08F, 520.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kTriggerGoal, "Gameplay", "SensorVolume3D + TriggerVolumeModifier") },

    { 119, "Camera3D", "CameraLens3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "18:22",
      "Includes 5 instant scene bookmarks, WASD + RMB free-look, Focus Selected (`F`), and physical exposure/DoF",
      "Reversed-Z infinite perspective matrix + jittered TAA motion-vector velocity history",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.65F, 0.65F, 0.85F }, { 0.35F, 0.88F, 1.00F }, 0.80F, 0.18F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvTonemap, "Default", "CameraLens3D + PhysicalOptics + Bookmarks") },

    { 120, "SpringArm3D", "SpringArmCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "24:37",
      "Folds inline onto `AvatarVessel3D` or nests as a child — smooth 3D X/Y/Z spring follow + RMB orbit!",
      "Obstacle-swept spring damper (`std::clamp(dt * 7.5F, 0.05F, 1.0F)`) with zero 1-frame camera lag",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.45F, 0.45F, 1.25F }, { 0.42F, 0.92F, 0.98F }, 0.75F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Characters", "SpringArmCapsule3D + ObstacleSweepLag") },

    { 121, "VehicleBody3D", "RoverVessel3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "18:46",
      "Spawns a complete drivable/kinematic multi-wheel rover chassis without manually parenting 4 separate wheel nodes",
      "Raycast suspension spring-damper + Pacejka lateral/longitudinal tire friction model",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.85F, 0.62F, 1.15F }, { 0.95F, 0.58F, 0.18F }, 0.82F, 0.18F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover,
                             "Physics", "RoverVessel3D + SuspensionChassis + CarbonPBR") },

    { 122, "VehicleWheel3D", "SuspensionWheelCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "18:46",
      "Can be nested as a child wheel OR folded as a 4-wheel array capsule on `RoverVessel3D`",
      "Sub-stepped suspension travel raycast + Ackermann steering geometry",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.65F, 0.35F, 0.65F }, { 0.22F, 0.25F, 0.30F }, 0.35F, 0.55F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider,
                             "Physics", "SuspensionWheelCapsule3D + AckermannTire") },

    { 123, "Joint3D", "XpbdConstraintBase3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:02",
      "Folds inline onto any `KineticBody3D` with 3D constraint axis & limit arc visualization",
      "6-DOF XPBD compliance matrix solver with breakable impulse threshold",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.55F, 0.55F, 0.55F }, { 0.95F, 0.78F, 0.28F }, 0.80F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Physics", "XpbdConstraintBase3D + 6DofXPBD") },

    { 124, "HingeJoint3D", "RevoluteHingeCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:13",
      "1-click door/catapult/pendulum hinge with motor RPM, torque limit, and min/max angle stops",
      "Quaternion swing-twist decomposition hinge constraint",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.45F, 0.95F, 0.45F }, { 0.92F, 0.68F, 0.24F }, 0.85F, 0.18F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover | NodeSpawnFlags::kBoxCollider,
                             "Physics", "RevoluteHingeCapsule3D + SwingTwistHinge") },

    { 125, "ConeTwistJoint3D", "RagdollConeTwistCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:24",
      "Anatomical shoulder/hip/neck joint limits with visual swing-cone gizmo in the viewport",
      "Elliptical cone swing + twist quaternion projection",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.65F, 0.85F, 0.65F }, { 0.94F, 0.54F, 0.32F }, 0.75F, 0.22F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kCapsuleCollider, "Physics", "RagdollConeTwistCapsule3D + EllipticalCone") },

    { 126, "SliderJoint3D", "PrismaticPistonCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:32",
      "Hydraulic piston / elevator rail with built-in oscillation and rider carry",
      "Prismatic axis projection + linear servo motor drive",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.50F, 1.40F, 0.50F }, { 0.35F, 0.84F, 0.96F }, 0.88F, 0.16F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover | NodeSpawnFlags::kBoxCollider,
                             "Physics", "PrismaticPistonCapsule3D + LinearServo") },

    { 127, "PinJoint3D", "BallSocketPinCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:42",
      "Zero-stretch ball-and-socket pivot for chains, wrecking balls, and hanging lanterns",
      "3-DOF positional XPBD point-to-point constraint",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.60F, 0.60F, 0.60F }, { 0.96F, 0.82F, 0.30F }, 0.90F, 0.14F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider,
                             "Physics", "BallSocketPinCapsule3D + PointConstraint") },

    { 128, "Generic6DOFJoint3D", "SixDofMotorConstraint3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "19:55",
      "Clean per-axis (X/Y/Z Linear + Pitch/Yaw/Roll Angular) Lock/Free/Limited/Spring matrix UI",
      "Full 6x6 Delassus effective-mass matrix solver with per-axis spring compliance",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 0.85F, 0.85F, 0.85F }, { 0.86F, 0.45F, 0.96F }, 0.85F, 0.18F, 380.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "SixDofMotorConstraint3D + Delassus6x6") },

    { 129, "Skeleton3D", "RiggedSkeleton3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "20:11",
      "Auto-loads glTF 2.0 skinning hierarchy (`CesiumMan.glb`) with live timeline scrub and speed control",
      "GPU skinning vertex shader + per-frame dynamic BLAS refit for ray-traced character shadows!",
      detail_preset::make_3d(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 1.0F, 1.0F, 1.0F }, 0.0F, 0.6F, 0.0F, 1,
                             NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController, "Characters", "RiggedSkeleton3D + DynamicBlasRefit") },

    { 130, "BoneAttachment3D", "NestedBoneSocket3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "20:31",
      "Nests any weapon, light, or particle emitter onto a character hand/head socket in 1 click",
      "Direct bone-palette matrix multiplication in `Scene::update_transforms()`",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 0.45F, 0.45F, 0.95F }, { 0.30F, 0.90F, 0.98F }, 0.90F, 0.15F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Characters", "NestedBoneSocket3D + BoneMatrixSocket") },

    { 131, "PhysicalBone3D", "RagdollBoneLink3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "41:25",
      "Auto-fits capsule colliders to bone lengths without manual per-bone sizing",
      "Articulated body featherstone / XPBD hybrid ragdoll link",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.42F, 0.95F, 0.42F }, { 0.92F, 0.62F, 0.38F }, 0.35F, 0.38F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider,
                             "Characters", "RagdollBoneLink3D + AutoFitCapsule") },

    { 132, "SkeletonIK3D", "AnalyticalTwoBoneIkCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "40:43",
      "Not deprecated like Godot's SkeletonIK3D! Built-in foot-ground raycast placement and look-at target",
      "Closed-form law-of-cosines 2-bone IK + pole-vector twist + FABRIK multi-chain fallback",
      detail_preset::make_3d(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 0.88F, 0.96F, 1.00F }, 0.0F, 0.5F, 0.0F, 1,
                             NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController, "Characters", "AnalyticalTwoBoneIkCapsule3D + FootPlacementIK") },

    { 133, "RootMotionView", "RootTrajectoryVisualizer3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "20:45",
      "Visualizes past and predicted future character motion trajectory and footstep contacts in 3D",
      "Ring-buffered root transform delta extractor + ground grid projection",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 1.2F, 0.15F, 1.2F }, { 0.25F, 0.95F, 0.68F }, 0.50F, 0.25F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Characters", "RootTrajectoryVisualizer3D + TrajectoryArc") },

    { 134, "VoxelGI", "IrradianceField3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "21:14",
      "Zero offline voxel baking wait! Updates diffuse global illumination dynamically every frame",
      "Hardware Ray-Traced Dynamic Diffuse Global Illumination (`cd::ddgi` octahedral irradiance + visibility probes)",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.15F, 1.15F, 1.15F }, { 0.98F, 0.96F, 0.92F }, 1.00F, 0.04F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvSkyDdgi | NodeSpawnFlags::kSphereCollider,
                             "Lights", "IrradianceField3D + RayTracedDDGI") },

    { 135, "LightmapGI", "GpuBakedLightmapField3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "21:30",
      "Progressive GPU path-traced lightmap preview directly in the viewport with instant denoise",
      "Compute ray-query path tracer + À-Trous SVGF / OIDN-style bilateral wavelet denoiser (`cd::denoise`)",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.2F, 1.2F, 1.2F }, { 0.96F, 0.92F, 0.84F }, 0.15F, 0.28F, 0.0F, 5,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvSkyDdgi | NodeSpawnFlags::kBoxCollider,
                             "Lights", "GpuBakedLightmapField3D + SvgfDenoise") },

    { 136, "LightmapProbe", "SphericalHarmonicProbe3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "21:30",
      "Auto-places L2 Spherical Harmonics probes across navigable space without manual clicking",
      "3rd-order Spherical Harmonics (L2 SH) irradiance + tetrahedral barycentric interpolation",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.65F, 0.65F, 0.65F }, { 0.94F, 0.95F, 0.99F }, 1.00F, 0.06F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Lights", "SphericalHarmonicProbe3D + L2SH") },

    { 137, "ReflectionProbe", "RadianceProbe3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "21:44",
      "Combines GGX pre-filtered IBL cubemap with real-time hardware BVH ray-traced mirror reflections!",
      "Split-sum GGX LUT + `VK_KHR_ray_query` inline BVH specular reflection trace in `prim.frag.glsl`",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 1.10F, 1.10F, 1.10F }, { 0.96F, 0.97F, 0.99F }, 1.00F, 0.04F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider | NodeSpawnFlags::kEnvSkyDdgi,
                             "Lights", "RadianceProbe3D + HardwareRayQueryMirror") },

    { 138, "VisibleOnScreenNotifier3D", "HiZVisibilitySensor3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "22:20",
      "Detects BOTH frustum visibility AND true occlusion behind walls (unlike Godot's frustum-only check)",
      "GPU Hierarchical-Z (Hi-Z) depth pyramid occlusion test (`cd::visibility`)",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 0.85F, 0.85F, 0.85F }, { 0.35F, 0.94F, 0.72F }, 0.30F, 0.30F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Default", "HiZVisibilitySensor3D + HiZOcclusion") },

    { 139, "VisibleOnScreenEnabler3D", "HiZSimulationGateCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "22:20",
      "Automatically throttles distant/occluded physics & animation rates with smooth LOD wake-up",
      "Distance + Hi-Z visibility-gated tick frequency decimation",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 0.85F, 0.85F, 0.85F }, { 0.30F, 0.88F, 0.64F }, 0.30F, 0.32F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Default", "HiZSimulationGateCapsule3D + TickLOD") },

    { 140, "GridMap", "VoxelTilePaletteMap3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "22:32",
      "Includes rule-based auto-tiling, WFC level generation, and instant merged static collision",
      "Octree-chunked GPU instancing + greedy face-culled mesh & BLAS builder",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.0F, 0.5F, 2.0F }, { 0.45F, 0.78F, 0.92F }, 0.20F, 0.32F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "VoxelTilePaletteMap3D + OctreeChunking") },

    { 141, "AudioListener3D", "AudioEar3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "22:50",
      "Auto-synced with active 3D camera or player `AvatarVessel3D` head orientation",
      "MIT KEMAR binaural HRTF head-related impulse response convolution (`cd::audio`)",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.55F, 0.55F, 0.55F }, { 0.38F, 0.86F, 0.98F }, 0.60F, 0.22F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Default", "AudioEar3D + KemarBinauralHRTF") },

    { 142, "AudioStreamPlayer3D", "SoundSource3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "22:59",
      "Spawns a 3D spatial sound emitter with Doppler shift, air absorption, and reverb send sliders",
      "HRTF azimuth/elevation binaural spatializer + ray-traced occlusion attenuation",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.65F, 0.65F, 0.65F }, { 0.98F, 0.68F, 0.24F }, 0.85F, 0.16F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Default", "SoundSource3D + HrtfSpatializer") },

    { 143, "NavigationRegion3D", "NavMeshZone3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "23:13",
      "1-click Recast-style voxel navmesh generation directly from scene colliders with slope/climb filters",
      "Tiled recast heightfield voxelizer + Detour-style polygon corridor graph",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.8F, 0.18F, 2.8F }, { 0.22F, 0.78F, 0.94F }, 0.18F, 0.45F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Gameplay", "NavMeshZone3D + TiledNavMesh") },

    { 144, "NavigationObstacle3D", "DynamicRvoObstacle3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "23:13",
      "Folds inline onto moving crates/vehicles so `WayfinderAgent3D` bots steer around them in real time",
      "3D Cylindrical Velocity Obstacle (RVO2-3D) reciprocal avoidance",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.90F, 1.20F, 0.90F }, { 0.96F, 0.54F, 0.22F }, 0.40F, 0.30F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider,
                             "Gameplay", "DynamicRvoObstacle3D + RvoAvoidance") },

    { 145, "NavigationAgent3D", "WayfinderAgent3D", StudioNodeCategory::Node3D, ChromaConstructKind::Pilot, "23:13",
      "Includes autonomous waypoint patrol AND player pursuit (`PatrolWaypointModifier`) out of the box!",
      "A* funnel pathfinding + player-range pursuit + plasma projectile target lock",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.85F, 1.15F, 0.85F }, { 0.92F, 0.35F, 0.88F }, 0.92F, 0.15F, 380.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kPatrolAi | NodeSpawnFlags::kTriggerHazard | NodeSpawnFlags::kBoxCollider,
                             "Characters", "WayfinderAgent3D + PatrolWaypointAI + Hazard") },

    { 146, "NavigationLink3D", "OffMeshTraversalLink3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "23:13",
      "Connects ledges, jump pads, elevators, and ladders for AI agents with 1-click endpoints",
      "Parabolic ballistic jump trajectory + state-machine traversal trigger",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 1.35F, 0.24F, 1.35F }, { 0.18F, 0.98F, 0.56F }, 0.65F, 0.16F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kTriggerJumpPad,
                             "Gameplay", "OffMeshTraversalLink3D + JumpPadBoost") },

    { 147, "OccluderInstance3D", "HiZOccluderHull3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "23:39",
      "No manual occluder mesh authoring required — every opaque mesh automatically feeds the GPU Hi-Z pyramid!",
      "2-phase GPU Hi-Z depth pyramid occlusion culling (`cd::visibility`)",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 2.0F, 1.8F, 0.40F }, { 0.48F, 0.52F, 0.60F }, 0.15F, 0.55F, 0.0F, 3,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Geometry", "HiZOccluderHull3D + AutoGpuHiZ") },

    { 148, "Path3D", "SplineTrack3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "24:03",
      "3D Catmull-Rom / Cubic Bezier spline with banking roll angle and constant-velocity table",
      "Bishop parallel-transport frame solver (eliminates Frenet Gimbal flip at inflection points)",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 1.6F, 0.35F, 1.6F }, { 0.96F, 0.78F, 0.26F }, 0.75F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kKinematicMover, "Gameplay", "SplineTrack3D + BishopFrameSpline") },

    { 149, "PathFollow3D", "SplineCarriageCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "24:03",
      "Folds directly onto any platform, camera, or train car with speed/loop/ping-pong controls",
      "Arc-length parameterized spline carriage + rider velocity inheritance",
      detail_preset::make_3d(CatalogPrimKind::Cube, { 1.8F, 0.30F, 1.2F }, { 0.28F, 0.82F, 0.96F }, 0.78F, 0.20F, 0.0F, 4,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyKinematic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover,
                             "Gameplay", "SplineCarriageCapsule3D + MovingCarriage") },

    { 150, "RayCast3D", "RayProbe3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "24:16",
      "Can query EITHER the CPU physics world OR the GPU hardware BVH (`VK_KHR_ray_query`)!",
      "Analytical OBB/Sphere/Capsule ray intersection + hardware TLAS ray-query unified API",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.45F, 1.10F, 0.45F }, { 0.22F, 0.96F, 0.58F }, 0.60F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Physics", "RayProbe3D + CpuAndGpuRayCast") },

    { 151, "ShapeCast3D", "ConvexSweepProbe3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "24:27",
      "Sweeps a Sphere, Capsule, or Box volume with live 3D contact manifold visualization",
      "Continuous GJK-EPA convex sweep + multi-contact normal & penetration depth output",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 0.65F, 1.10F, 0.65F }, { 0.28F, 0.92F, 0.74F }, 0.55F, 0.22F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kCapsuleCollider, "Physics", "ConvexSweepProbe3D + GjkConvexSweep") },

    { 152, "RemoteTransform3D", "TransformRelayCapsule3D", StudioNodeCategory::Node3D, ChromaConstructKind::TraitCapsule, "24:55",
      "Supports spring-damped position/rotation relay and selective axis masking in 1 inline capsule",
      "Topological transform dependency sort (zero 1-frame lag)",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.55F, 0.55F, 0.55F }, { 0.62F, 0.80F, 0.98F }, 0.70F, 0.20F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Default", "TransformRelayCapsule3D + SpringRelay") },

    { 153, "XRNode3D", "OpenXrSpatialElement3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "25:08",
      "Includes desktop mouse/keyboard 6-DOF XR simulator so you can test VR interactions without a headset!",
      "OpenXR pose action binding + late-latching pose prediction before command buffer submit",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.65F, 0.65F, 0.65F }, { 0.45F, 0.78F, 1.00F }, 0.85F, 0.16F, 400.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Default", "OpenXrSpatialElement3D + DesktopXrSim") },

    { 154, "XRCamera3D", "OpenXrStereoLens3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "25:08",
      "Single-pass stereo multiview rendering with comfort vignette and asymmetric FOV support",
      "`VK_KHR_multiview` single-draw-call binocular instancing + foveated rendering hooks",
      detail_preset::make_3d(CatalogPrimKind::Cone, { 0.65F, 0.55F, 0.75F }, { 0.38F, 0.86F, 1.00F }, 0.90F, 0.14F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kEnvTonemap, "Default", "OpenXrStereoLens3D + VkMultiview") },

    { 155, "XROrigin3D", "OpenXrStageOrigin3D", StudioNodeCategory::Node3D, ChromaConstructKind::Stage, "25:08",
      "Built-in teleport arc, snap-turn, and guardian boundary grid visualization",
      "Reference-space floor alignment + comfort locomotion frame transformer",
      detail_preset::make_3d(CatalogPrimKind::Cylinder, { 1.5F, 0.15F, 1.5F }, { 0.28F, 0.88F, 0.96F }, 0.65F, 0.22F, 0.0F, 2,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider,
                             "Default", "OpenXrStageOrigin3D + TeleportLocomotion") },

    { 156, "XRController3D", "OpenXrHandPilot3D", StudioNodeCategory::Node3D, ChromaConstructKind::Pilot, "25:08",
      "Pre-wired grab magnet, laser UI pointer, and haptic pulse feedback without boilerplate scripts",
      "Physics velocity-tracked kinematic hand grabber + skeletal hand-tracking pose solver",
      detail_preset::make_3d(CatalogPrimKind::Sphere, { 0.45F, 0.45F, 0.65F }, { 0.32F, 0.94F, 0.82F }, 0.80F, 0.18F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kSphereCollider, "Characters", "OpenXrHandPilot3D + PhysicsGrabber") },

    { 157, "XRAnchor3D", "OpenXrSpatialAnchor3D", StudioNodeCategory::Node3D, ChromaConstructKind::Element, "25:08",
      "Persists real-world room-scale AR/MR anchors to `.cdscene.json` automatically",
      "`XR_MSFT_spatial_anchor` / `XR_FB_spatial_entity` UUID persistence bridge",
      detail_preset::make_3d(CatalogPrimKind::Torus, { 0.65F, 0.65F, 0.65F }, { 0.98F, 0.82F, 0.28F }, 0.88F, 0.15F, 0.0F, 0,
                             NodeSpawnFlags::kPbrMaterial | NodeSpawnFlags::kBoxCollider, "Default", "OpenXrSpatialAnchor3D +PersistentUUID") },

    { 158, "PhysicalBoneSimulator3D", "RagdollChain3D", StudioNodeCategory::Node3D, ChromaConstructKind::Vessel, "41:25",
      "1-slider blend from 100% keyframe animation to partial hit-reaction or full ragdoll physics!",
      "XPBD articulated skeleton solver + self-collision capsule pruning",
      detail_preset::make_3d(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 0.98F, 0.78F, 0.62F }, 0.0F, 0.5F, 0.0F, 1,
                             NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider, "Characters", "RagdollChain3D + ActiveRagdollBlend") },

    // =========================================================================
    // 4. CHROMAUI / CONTROL NODES (60 Nodes, IDs 159..218)
    // =========================================================================
    { 159, "Control", "UiFrame", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:54",
      "Combines immediate-mode simplicity (`ImGui::*`) with retained Flexbox/Anchor layout and hardware scissor clipping",
      "`cd::ui::ChromaUi` 6-variant GPU shader + intersected `clip_stack_` overflow protection",
      detail_preset::make_ui({ 0.28F, 0.68F, 0.96F }, "UiFrame + FlexAnchorLayout + ClipStack") },

    { 160, "Container", "UiFlexContainer", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "25:35",
      "Unified CSS-Flexbox + Grid + Anchor container — no need to swap node types to change layout direction",
      "Single-pass O(N) constraint layout solver (`cd::ui::layout`)",
      detail_preset::make_ui({ 0.30F, 0.72F, 0.94F }, "UiFlexContainer + O(N)FlexSolver") },

    { 161, "AspectRatioContainer", "UiAspectFrame", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "25:44",
      "Locks 16:9, 21:9, 4:3, or 1:1 aspect ratio with letterbox or cover-fill modes in 1 click",
      "Sub-pixel aligned viewport rectfitter",
      detail_preset::make_ui({ 0.34F, 0.74F, 0.92F }, "UiAspectFrame + AspectLock") },

    { 162, "BoxContainer", "UiAxisBoxContainer", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "25:59",
      "Supports gap spacing, justify-content, align-items, and animated child reordering",
      "SIMD-accelerated axis extent accumulation",
      detail_preset::make_ui({ 0.36F, 0.76F, 0.90F }, "UiAxisBoxContainer + FlexGap") },

    { 163, "VBoxContainer", "UiVerticalStack", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "25:59",
      "Includes pinned header/footer regions + scrollable middle child (`BeginChild`) out of the box",
      "Zero-reallocation vertical cursor layout with automatic `content_max_y` tracking",
      detail_preset::make_ui({ 0.38F, 0.78F, 0.88F }, "UiVerticalStack + PinnedHeaderScroll") },

    { 164, "HBoxContainer", "UiHorizontalRow", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "25:59",
      "Responsive horizontal toolbar row that automatically wraps or collapses overflow items into a popup",
      "Proportional flex-grow / flex-shrink width distributor",
      detail_preset::make_ui({ 0.40F, 0.80F, 0.86F }, "UiHorizontalRow + FlexStretch") },

    { 165, "CenterContainer", "UiAnchorCenterFrame", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:13",
      "Centers any modal, crosshair, or HUD widget with optional spring bounce-in animation",
      "DPI-aware half-extent pivot aligner",
      detail_preset::make_ui({ 0.42F, 0.82F, 0.84F }, "UiAnchorCenterFrame + DpiCenter") },

    { 166, "FlowContainer", "UiWrapFlowLayout", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:21",
      "Ideal for tag clouds, asset cards, and inventory badges with automatic line wrapping",
      "Multi-line row-height equalizer pass",
      detail_preset::make_ui({ 0.44F, 0.84F, 0.82F }, "UiWrapFlowLayout + TagCloudWrap") },

    { 167, "VFlowContainer", "UiVerticalWrapFlow", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:21",
      "Column-first wrapping layout with synchronized column widths",
      "Vertical column-break layout solver",
      detail_preset::make_ui({ 0.46F, 0.86F, 0.80F }, "UiVerticalWrapFlow + ColumnWrap") },

    { 168, "HFlowContainer", "UiHorizontalWrapFlow", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:21",
      "Row-first responsive card flow that adapts from 1 to 8 columns smoothly",
      "Horizontal line-break layout solver",
      detail_preset::make_ui({ 0.48F, 0.88F, 0.78F }, "UiHorizontalWrapFlow + ResponsiveCards") },

    { 169, "GridContainer", "UiGridMatrixContainer", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:35",
      "Supports column span, row span, and stretch-same / stretch-proportional sizing (`BeginTable`)",
      "`ChromaUi::TableState` N-column hardware-clipped cell layout",
      detail_preset::make_ui({ 0.32F, 0.82F, 0.92F }, "UiGridMatrixContainer + TableCells") },

    { 170, "SplitContainer", "UiSashSplitter", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:49",
      "Interactive draggable sash with double-click collapse/restore and proportional resize",
      "`DockSpace` interactive divider rail with min/max pane clamping",
      detail_preset::make_ui({ 0.36F, 0.78F, 0.94F }, "UiSashSplitter + InteractiveSash") },

    { 171, "VSplitContainer", "UiVerticalSplitter", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:49",
      "Top/Bottom split pane with persistent split ratio across sessions",
      "Vertical sash hit-test & hover highlight renderer",
      detail_preset::make_ui({ 0.38F, 0.76F, 0.96F }, "UiVerticalSplitter + TopBottomSplit") },

    { 172, "HSplitContainer", "UiHorizontalSplitter", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "26:49",
      "Left/Right split pane used in the Content Browser Folder Tree + Asset Grid",
      "Horizontal sash hit-test & live layout reflow",
      detail_preset::make_ui({ 0.40F, 0.74F, 0.98F }, "UiHorizontalSplitter + LeftRightSplit") },

    { 173, "MarginContainer", "UiPaddingInsetFrame", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "27:02",
      "Built directly into every `UiFrame` as `padding` — or usable standalone for responsive safe-area insets",
      "Zero-extra-draw-call cursor inset modifier",
      detail_preset::make_ui({ 0.45F, 0.72F, 0.92F }, "UiPaddingInsetFrame + SafeAreaInset") },

    { 174, "PanelContainer", "UiFrostedCardContainer", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "27:15",
      "Auto-sizes to child content with rounded SDF border, gradient fill, and drop shadow",
      "SDF rounded-rect (`variant == 7u`) + backdrop blur (`variant == 5u`)",
      detail_preset::make_ui({ 0.26F, 0.34F, 0.48F }, "UiFrostedCardContainer + SdfRoundedRect") },

    { 175, "ScrollContainer", "UiSmoothScrollArea", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "27:24",
      "Interactive draggable scrollbar thumb, mouse-wheel popup-occlusion guard, and drag-and-drop edge auto-scroll!",
      "`BeginChild` / `EndChild` hardware scissor clip + kinetic wheel accumulator",
      detail_preset::make_ui({ 0.30F, 0.64F, 0.88F }, "UiSmoothScrollArea + EdgeAutoScroll") },

    { 176, "TabContainer", "UiDockTabWorkspace", StudioNodeCategory::ControlUI, ChromaConstructKind::Stage, "27:41",
      "Supports multi-tab docking, undocking to floating windows, and programmatic tab switching",
      "`BeginTabBar` / `BeginTabItem` + `DockBuilderDockWindow` slot manager",
      detail_preset::make_ui({ 0.35F, 0.70F, 0.95F }, "UiDockTabWorkspace + MultiTabDock") },

    { 177, "Label", "UiGlyphText", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:09",
      "Proportional TrueType (`Segoe UI`) + crisp 5x7 fallback with automatic UTF-8 em-dash normalization",
      "`cd::ui::font::Font` 512x512 RGBA8 glyph atlas + kerning & BiDi RTL layout support",
      detail_preset::make_ui({ 0.88F, 0.92F, 0.98F }, "UiGlyphText + TrueTypeAtlas + UTF8") },

    { 178, "RichTextLabel", "UiRichMarkupText", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:15",
      "Inline color tags, bold/header scales, icon badges (`DrawIcon`), and animated typewriter reveal",
      "Single-pass markup tokenizer + multi-style glyph quad batcher",
      detail_preset::make_ui({ 0.92F, 0.86F, 0.68F }, "UiRichMarkupText + InlineBadges") },

    { 179, "ColorRect", "UiGradientQuad", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:24",
      "Supports solid tint, 4-corner linear/radial gradients, and rounded corners in 1 widget",
      "GPU `kLinearGrad` (`variant == 4u`) + `kRoundedRect` (`variant == 7u`) shader",
      detail_preset::make_ui({ 0.25F, 0.78F, 0.95F }, "UiGradientQuad + LinearRadialGrad") },

    { 180, "TextureRect", "UiBindlessSpriteQuad", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:30",
      "Displays any disk texture, procedural material preview, or live 3D render target with UV zoom/pan",
      "`Submitter::set_texture_slot` multi-atlas descriptor binding (`variant == 1u`)",
      detail_preset::make_ui({ 0.95F, 0.95F, 0.98F }, "UiBindlessSpriteQuad + MultiAtlasSlot") },

    { 181, "VideoStreamPlayer", "UiGpuVideoSurface", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:36",
      "Plays cutscenes or animated tutorial clips inside any UI card or 3D screen surface",
      "Double-buffered YUV-to-RGB compute conversion + ring-buffer audio sync",
      detail_preset::make_ui({ 0.82F, 0.38F, 0.92F }, "UiGpuVideoSurface + YuvComputeDecode") },

    { 182, "Separator", "UiDividerRule", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:41",
      "Supports labeled section headers (`SeparatorText`) with accent color underline",
      "1-quad batched horizontal/vertical rule with theme-aware contrast",
      detail_preset::make_ui({ 0.45F, 0.52F, 0.64F }, "UiDividerRule + SeparatorText") },

    { 183, "HSeparator", "UiHorizontalRule", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:41",
      "Crisp 1px horizontal divider with optional titled badge",
      "Pixel-snapped horizontal line primitive",
      detail_preset::make_ui({ 0.48F, 0.55F, 0.66F }, "UiHorizontalRule + PixelSnapped") },

    { 184, "VSeparator", "UiVerticalRule", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:41",
      "Inline vertical toolbar separator that auto-matches current row height",
      "Pixel-snapped vertical line primitive",
      detail_preset::make_ui({ 0.48F, 0.55F, 0.66F }, "UiVerticalRule + RowHeightMatch") },

    { 185, "Panel", "UiElevationSurfacePanel", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "28:51",
      "Switches across 5 built-in Studio themes (*Chroma Carbon Dark*, *Unreal Slate*, *Blender Graphite*, *Cyberpunk*, *Daylight*)",
      "Theme-driven SDF rounded surface + elevation shadow",
      detail_preset::make_ui({ 0.22F, 0.26F, 0.34F }, "UiElevationSurfacePanel + 5Themes") },

    { 186, "NinePatchRect", "UiSdfNineSliceFrame", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:04",
      "No 9-slice texture asset required for crisp borders — or use textured 9-patch (`variant == 3u`) when desired!",
      "Analytical shader 9-patch UV remapping (`kNinePatch` `variant == 3u`) + SDF border",
      detail_preset::make_ui({ 0.38F, 0.68F, 0.92F }, "UiSdfNineSliceFrame + Variant3NinePatch") },

    { 187, "BaseButton", "UiInteractiveResponder", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:35",
      "Built-in hover, active-press, keyboard focus ring, tooltip, and drag-and-drop source/target hooks",
      "Zero-allocation immediate/retained ID state machine",
      detail_preset::make_ui({ 0.28F, 0.56F, 0.88F }, "UiInteractiveResponder + FocusRing") },

    { 188, "Button", "UiCommandButton", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:35",
      "Supports vector icons (`DrawIcon`), shortcut badges, and hardware-clipped labels that never overflow",
      "Coalesced SDF button quad + proportional TrueType centered text",
      detail_preset::make_ui({ 0.22F, 0.52F, 0.86F }, "UiCommandButton + VectorIcon + Clip") },

    { 189, "TextureButton", "UiIconCommandButton", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:35",
      "Includes 14 procedural vector icons (Play, Pause, Stop, Cube, Sphere, Light, Folder, Gear, Search, etc.) built-in!",
      "`ChromaUi::DrawIcon` anti-aliased vector geometry + bindless sprite quad",
      detail_preset::make_ui({ 0.26F, 0.62F, 0.92F }, "UiIconCommandButton + 14VectorIcons") },

    { 190, "LinkButton", "UiHyperlinkAction", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:48",
      "Opens documentation/ADRs, focuses scene entities, or switches levels on click with hover underline",
      "URI / internal `cd://` asset path router",
      detail_preset::make_ui({ 0.35F, 0.82F, 1.00F }, "UiHyperlinkAction + AssetRouter") },

    { 191, "CheckBox", "UiBooleanCheckBox", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:56",
      "Crisp vector checkmark (`DrawIcon::Check`) with full-row click target and Undo/Redo recording",
      "Anti-aliased polyline checkmark + theme accent fill",
      detail_preset::make_ui({ 0.25F, 0.84F, 0.56F }, "UiBooleanCheckBox + VectorCheck") },

    { 192, "CheckButton", "UiAnimatedTogglePill", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "29:56",
      "Modern iOS/Studio-style animated pill switch (`ImGui::ToggleSwitch`) with glowing active state",
      "Smooth spring-interpolated thumb position + rounded pill SDF",
      detail_preset::make_ui({ 0.20F, 0.88F, 0.62F }, "UiAnimatedTogglePill + ToggleSwitch") },

    { 193, "MenuButton", "UiPopupMenuButton", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:04",
      "Opens a screen-clamped cascading popup menu with 1 click",
      "`BeginPopup` z-layering + automatic outside-click dismissal",
      detail_preset::make_ui({ 0.32F, 0.68F, 0.94F }, "UiPopupMenuButton + ScreenClampedPopup") },

    { 194, "OptionButton", "UiSearchableComboDropdown", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:11",
      "Supports both simple arrays (`ImGui::Combo`) and rich dynamic items (`BeginCombo`/`EndCombo`)",
      "Popup-occluded wheel scroll + screen-boundary clamp",
      detail_preset::make_ui({ 0.34F, 0.72F, 0.96F }, "UiSearchableComboDropdown + BeginCombo") },

    { 195, "ColorPickerButton", "UiHdrSwatchButton", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:22",
      "Displays live RGB values + `#RRGGBB` hex preview swatch that pops up an interactive editor on click",
      "`ImGui::ColorEdit3` inline swatch + modal popup RGB/HSV sliders",
      detail_preset::make_ui({ 0.96F, 0.52F, 0.32F }, "UiHdrSwatchButton + HexSwatchPopup") },

    { 196, "ColorPicker", "UiOklabHdrColorPicker", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:22",
      "Includes Kelvin temperature (`1800K..15000K`), `#RRGGBB` hex, and linear HDR intensity multipliers",
      "Linear sRGB <-> Kelvin CCT (`cd::light::cct_to_linear_rgb`) + gradient slider bars",
      detail_preset::make_ui({ 0.98F, 0.64F, 0.28F }, "UiOklabHdrColorPicker + KelvinAndHex") },

    { 197, "LineEdit", "UiTextInputField", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:30",
      "Blinking caret, Left/Right arrows, Home/End, Backspace/Delete, placeholder hints (`InputTextWithHint`), and `WantCaptureKeyboard` guard!",
      "UTF-8 code-point cursor navigation + horizontal text scroll clipping",
      detail_preset::make_ui({ 0.28F, 0.76F, 0.92F }, "UiTextInputField + CaretAndHint") },

    { 198, "TextEdit", "UiMultilineDocumentBox", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:38",
      "Smooth vertical/horizontal scrolling for logs, JSON inspection, and dialogue scripts",
      "Virtualized visible-line-only rendering for 100,000+ line logs",
      detail_preset::make_ui({ 0.30F, 0.70F, 0.88F }, "UiMultilineDocumentBox + VirtualizedLines") },

    { 199, "CodeEdit", "UiGlslSyntaxEditor", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "30:38",
      "Edits `.glsl` shaders or `.cdscene.json` with live background SPIR-V hot-reload on save",
      "`StudioShaderWatch` + `cd::gluon` live compiler diagnostics overlay",
      detail_preset::make_ui({ 0.26F, 0.82F, 0.78F }, "UiGlslSyntaxEditor + SpirvHotReload") },

    { 200, "Range", "UiNumericValueModel", StudioNodeCategory::ControlUI, ChromaConstructKind::TraitCapsule, "30:54",
      "Shared min/max/step/format model across Sliders, DragBoxes, Knobs, and ProgressBars",
      "Double-click or `Ctrl+Click` inline keyboard numeric entry on any range widget!",
      detail_preset::make_ui({ 0.42F, 0.78F, 0.94F }, "UiNumericValueModel + InlineNumericEdit") },

    { 201, "ProgressBar", "UiGaugeBar", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:01",
      "Includes HUD presets for HP/Shield/Energy (`DrawHealthShieldBar`) and asset loading progress",
      "Smooth gradient fill + centered percentage/custom overlay text",
      detail_preset::make_ui({ 0.24F, 0.92F, 0.54F }, "UiGaugeBar + HealthShieldHUD") },

    { 202, "TextureProgressBar", "UiRadialProgressRing", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:01",
      "Draws crisp radial cooldown rings (`DrawInventoryHotbar`) and angular gauges without needing ring textures",
      "Procedural arc-segment tessellator + custom texture mask support",
      detail_preset::make_ui({ 0.28F, 0.90F, 0.76F }, "UiRadialProgressRing + CooldownArc") },

    { 203, "SpinBox", "UiInlineDragSpinBox", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:08",
      "Color-coded X (Red), Y (Green), Z (Blue) `DragFloat3` boxes withboth mouse drag AND inline typing!",
      "`ChromaUi::DragFloat` / `DragFloat3` dual drag + keyboard edit state machine",
      detail_preset::make_ui({ 0.92F, 0.48F, 0.36F }, "UiInlineDragSpinBox + XYZColorStrips") },

    { 204, "ScrollBar", "UiKineticScrollBar", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:17",
      "Auto-hides when content fits and supports direct thumb dragging + track page-click",
      "Proportional thumb-size & scroll-offset mapper",
      detail_preset::make_ui({ 0.40F, 0.62F, 0.84F }, "UiKineticScrollBar + ThumbDrag") },

    { 205, "HScrollBar", "UiHorizontalScrollBar", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:17",
      "Smooth horizontal timeline and node-graph pan bar",
      "Horizontal track & thumb hit-tester",
      detail_preset::make_ui({ 0.42F, 0.64F, 0.86F }, "UiHorizontalScrollBar + PanTrack") },

    { 206, "VScrollBar", "UiVerticalScrollBar", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:17",
      "Used across Scene Hierarchy, Inspector, and Content Browser with zero double-scrollbar artifacts",
      "Popup-occlusion-protected vertical thumb drag",
      detail_preset::make_ui({ 0.44F, 0.66F, 0.88F }, "UiVerticalScrollBar + OcclusionGuard") },

    { 207, "Slider", "UiValueSlider", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:33",
      "Includes rotary knob mode (`ImGui::KnobFloat`) AND linear slider with `Ctrl+Click` text input",
      "Sub-pixel thumb fill + custom `printf` unit formatting (`%.2f m`, `%.0f K`, `%.0f nm`)",
      detail_preset::make_ui({ 0.26F, 0.78F, 0.96F }, "UiValueSlider + KnobFloat + InlineEdit") },

    { 208, "HSlider", "UiHorizontalSlider", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:33",
      "Full-width responsive horizontal slider with filled accent track and inline value readout",
      "`ImGui::SliderFloat` / `SliderInt` with window clip-rect hit protection",
      detail_preset::make_ui({ 0.28F, 0.80F, 0.98F }, "UiHorizontalSlider + AccentTrack") },

    { 209, "VSlider", "UiVerticalSlider", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:33",
      "Ideal for audio mixer faders and EQ bands",
      "Vertical normalized range mapper",
      detail_preset::make_ui({ 0.30F, 0.82F, 0.96F }, "UiVerticalSlider + AudioFader") },

    { 210, "ItemList", "UiVirtualListView", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:41",
      "Supports multi-column icon/badge cards, instant search filtering, and drag-and-drop payloads",
      "O(visible) virtualized list clipper",
      detail_preset::make_ui({ 0.36F, 0.74F, 0.92F }, "UiVirtualListView + SearchAndDragDrop") },

    { 211, "MenuBar", "UiRibbonMenuBar", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "31:51",
      "Combines top MainMenuBar with a full-width Quick-Action Ribbon Toolbar (`y = 28..60`) and live FPS counter",
      "`BeginMainMenuBar` + cascading `BeginMenu` / `MenuItem` dispatcher",
      detail_preset::make_ui({ 0.24F, 0.68F, 0.94F }, "UiRibbonMenuBar + QuickRibbon + Telemetry") },

    { 212, "TabBar", "UiReorderableTabStrip", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:03",
      "Highlights active tab with glowing bottom accent bar and supports instant programmatic focus",
      "`st.tab_map[bar_id]` state table + dynamic label width layout",
      detail_preset::make_ui({ 0.32F, 0.72F, 0.96F }, "UiReorderableTabStrip + AccentUnderline") },

    { 213, "ReferenceRect", "UiDebugBoundsGuide", StudioNodeCategory::ControlUI, ChromaConstructKind::TraitCapsule, "32:12",
      "Can display layout bounds, safe-zones, or 3D selection outlines in both Edit and PIE modes",
      "`AddRect` dashed/solid overlay on background or foreground draw-list",
      detail_preset::make_ui({ 0.98F, 0.78F, 0.24F }, "UiDebugBoundsGuide + SafeZoneRect") },

    { 214, "Tree", "UiDragDropTreeOutliner", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:21",
      "Includes inline Visibility (`V`), Lock (`L`), and Modifier Badges (`[RB][BoxCol][MOV][TRG][AI]`) on every row!",
      "Drag-and-drop node reparenting (`CD_STUDIO_NODE_IDX`) + right-click context menu",
      detail_preset::make_ui({ 0.34F, 0.86F, 0.68F }, "UiDragDropTreeOutliner + VisLockBadges") },

    { 215, "GraphElement", "UiCanvasPinItem", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:29",
      "Typed input/output pins (`ImGui::NodePin`) with color-coded sockets and hover tooltips",
      "Screen-space pin coordinate registry for cubic Bezier wire routing",
      detail_preset::make_ui({ 0.45F, 0.78F, 0.98F }, "UiCanvasPinItem + TypedSockets") },

    { 216, "GraphNode", "UiShaderGraphBlock", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:29",
      "Draggable header-colored node block (`ImGui::BeginNode`/`EndNode`) for shaders, AI trees, and dialogues",
      "Per-node position persistence (`node_pos_map`) + active drag Z-elevation",
      detail_preset::make_ui({ 0.28F, 0.62F, 0.48F }, "UiShaderGraphBlock + DraggableHeader") },

    { 217, "GraphEdit", "UiNodeCanvas", StudioNodeCategory::ControlUI, ChromaConstructKind::Stage, "32:29",
      "Interactive grid canvas (`ImGui::BeginNodeGraph`) with cubic Bezier wires (`NodeLink`) + companion `CurveEditor` & `TimelineScrubber`",
      "Anti-aliased `AddBezierCubic` wire renderer + scrolled canvas coordinate transform",
      detail_preset::make_ui({ 0.22F, 0.54F, 0.78F }, "UiNodeCanvas + BezierWires + CurveEditor") },

    { 218, "TouchScreenButton", "UiMultiTouchVirtualPad", StudioNodeCategory::ControlUI, ChromaConstructKind::Element, "32:43",
      "Works seamlessly with touch, gamepad, AND mouse click for cross-platform mobile/desktop testing",
      "Multi-pointer touch ID tracker + radial virtual thumbstick deadzone mapper",
      detail_preset::make_ui({ 0.38F, 0.88F, 0.74F }, "UiMultiTouchVirtualPad + VirtualThumbstick") },

    // =========================================================================
    // 5. NEXT-GEN SOTA BEYOND-GODOT NODES (31 Nodes, IDs 219..249)
    // =========================================================================
    { 219, "HardwareRayTracingProbe3D", "HardwareRayTracingProbe3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "1-click hardware ray-traced mirror reflections & contact shadows — no screen-space disocclusion artifacts!",
      "Vulkan 1.3 `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` inline BVH traversal in `prim.frag.glsl`",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 1.15F, 1.15F, 1.15F }, { 0.96F, 0.98F, 1.00F }, 1.00F, 0.04F, 0.0F, 0,
                               NodeSpawnFlags::kSphereCollider | NodeSpawnFlags::kEnvSkyDdgi, "Lights",
                               "HardwareRayTracingProbe3D + VK_KHR_ray_query + MirrorSphere") },

    { 220, "DDGIIrradianceVolume3D", "DDGIIrradianceVolume3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Real-time bounce lighting without lightmap UV seams or VoxelGI light leaking through thin walls",
      "Ray-Traced Dynamic Diffuse Global Illumination (`cd::ddgi`) octahedral irradiance + Chebyshev visibility moments",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 1.10F, 1.10F, 1.10F }, { 1.00F, 0.92F, 0.76F }, 0.90F, 0.08F, 380.0F, 0,
                               NodeSpawnFlags::kSphereCollider | NodeSpawnFlags::kEnvSkyDdgi, "Lights",
                               "DDGIIrradianceVolume3D + OctahedralProbes + VisibilityMoments") },

    { 221, "TurquinMultiScatterPBR3D", "TurquinMultiScatterPBR3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Eliminates rough-metal energy darkening + adds physical thin-film iridescence (`0..800 nm`) in 2 sliders!",
      "Turquin (2019) Eq. 11 multiscatter compensation + Belcour & Barla (SIGGRAPH 2017) Airy thin-film interference (`cd::brdf_multiscatter`)",
      detail_preset::make_sota(CatalogPrimKind::TorusKnot, { 1.10F, 1.10F, 1.10F }, { 0.92F, 0.94F, 0.98F }, 1.00F, 0.16F, 440.0F, 0,
                               NodeSpawnFlags::kSphereCollider, "Geometry",
                               "TurquinMultiScatterPBR3D + Turquin2019 + BelcourBarla440nm") },

    { 222, "MeshletClusterLOD3D", "MeshletClusterLOD3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Renders high-poly hero sculptures without manual LOD0/LOD1/LOD2 mesh authoring or pop-in",
      "Nanite-style 64-triangle meshlet cluster DAG (`cd::virtual_geometry` + `cd::mesh_shader`) + GPU Hi-Z culling",
      detail_preset::make_sota(CatalogPrimKind::TorusKnot, { 1.20F, 1.20F, 1.20F }, { 1.00F, 0.78F, 0.34F }, 1.00F, 0.14F, 0.0F, 0,
                               NodeSpawnFlags::kBoxCollider, "Geometry",
                               "MeshletClusterLOD3D + ClusterDAG + HiZCull") },

    { 223, "NeuralTemporalUpscaler3D", "NeuralTemporalUpscaler3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Sharp sub-pixel anti-aliasing and temporal super-resolution folded directly into the camera stack",
      "Halton sub-pixel jitter + G-buffer motion vectors (`cd::velocity`) + YCoCg neighborhood-clamped TAAU (`cd::post_taa`)",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 0.90F, 0.90F, 0.90F }, { 0.35F, 0.88F, 1.00F }, 0.95F, 0.06F, 0.0F, 0,
                               NodeSpawnFlags::kEnvTonemap | NodeSpawnFlags::kSphereCollider, "Default",
                               "NeuralTemporalUpscaler3D + MotionVectorTAAU") },

    { 224, "XPBDClothAndSoftBody3D", "XPBDClothAndSoftBody3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Vessel, "42:00+ (Beyond Godot SOTA)",
      "Unconditionally stable cloth, ropes, and tetrahedral soft bodies that interact with rigid bodies",
      "Sub-stepped Extended Position-Based Dynamics (Macklin et al. 2016) distance, bending & tetrahedral volume constraints",
      detail_preset::make_sota(CatalogPrimKind::TorusKnot, { 1.05F, 1.05F, 1.05F }, { 0.94F, 0.96F, 1.00F }, 0.20F, 0.08F, 520.0F, 0,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kSphereCollider, "Physics",
                               "XPBDClothAndSoftBody3D + SubsteppedXPBD + SoapFilm520nm") },

    { 225, "FluidFLIPWater3D", "FluidFLIPWater3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Interactive water surface with floating rigid-body buoyancy, foam, and shoreline waves in 1 click",
      "Shallow-Water Equations (SWE) + PIC/FLIP hybrid grid-particle solver + FFT ocean spectrum (`cd::water`)",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 2.6F, 0.30F, 2.6F }, { 0.14F, 0.68F, 0.92F }, 0.90F, 0.06F, 360.0F, 0,
                               NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "Geometry",
                               "FluidFLIPWater3D + FftOcean + BuoyancyCoupling") },

    { 226, "SDFSmoothBooleanCSG3D", "SDFSmoothBooleanCSG3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Organic clay-like smooth boolean blending between primitives without sharp polygon creases",
      "Analytical Signed Distance Field polynomial smooth-minimum (`smin`) + real-time ray-marched / dual-contoured surface",
      detail_preset::make_sota(CatalogPrimKind::Torus, { 1.25F, 1.25F, 1.25F }, { 0.96F, 0.95F, 0.92F }, 0.18F, 0.14F, 0.0F, 5,
                               NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover,
                               "Geometry", "SDFSmoothBooleanCSG3D + SmoothUnionSDF + CarraraMarble") },

    { 227, "BrunetonPlanetarySky3D", "BrunetonPlanetarySky3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Physically accurate sunrise, noon, golden hour, and twilight just by rotating the sun direction!",
      "Bruneton & Neyret (2008) / Hillaire (2020) Transmittance, Multi-Scattering, Sky-View & Aerial Perspective 3D LUTs (`cd::atmosphere`)",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 1.25F, 1.25F, 1.25F }, { 0.42F, 0.78F, 1.00F }, 0.85F, 0.10F, 0.0F, 1,
                               NodeSpawnFlags::kEnvSkyDdgi | NodeSpawnFlags::kLightDirectional | NodeSpawnFlags::kSphereCollider,
                               "Lights", "BrunetonPlanetarySky3D + MultiScatterLUT + AerialPerspective") },

    { 228, "FroxelVolumetricFog3D", "FroxelVolumetricFog3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Cinematic god-ray light shafts and height-based ground mist with zero temporal flickering",
      "Wronski (2014) 3D camera-frustum froxel grid + Henyey-Greenstein phase function + temporal reprojection (`cd::volumetric_fog`)",
      detail_preset::make_sota(CatalogPrimKind::Cylinder, { 1.3F, 1.6F, 1.3F }, { 0.72F, 0.88F, 1.00F }, 0.20F, 0.60F, 0.0F, 0,
                               NodeSpawnFlags::kEnvFog | NodeSpawnFlags::kBoxCollider, "Lights",
                               "FroxelVolumetricFog3D + WronskiFroxels + GodRayShafts") },

    { 229, "GPUIndirectCrowd3D", "GPUIndirectCrowd3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Spawns and animates massive crowds or debris swarms with 1 draw call",
      "Compute-culled `vkCmdDrawIndexedIndirectCount` + Vertex Animation Texture (VAT) skeletal instancing",
      detail_preset::make_sota(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 0.90F, 0.95F, 1.00F }, 0.0F, 0.5F, 0.0F, 1,
                               NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kPatrolAi, "Characters",
                               "GPUIndirectCrowd3D + IndirectCount + VatCrowd") },

    { 230, "MotionMatchingInertial3D", "MotionMatchingInertial3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Responsive character locomotion without complex spaghetti state machines",
      "KD-tree pose-feature trajectory matching + Bollo (2018) fifth-order polynomial inertialization blending",
      detail_preset::make_sota(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 1.00F, 0.96F, 0.88F }, 0.0F, 0.5F, 0.0F, 1,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController,
                               "Characters", "MotionMatchingInertial3D + PoseFeatureKDTree") },

    { 231, "FullBodyFABRIK3D", "FullBodyFABRIK3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Simultaneous hands + feet + head look-at + pelvis balance IK in a single inline capsule",
      "Aristidou & Lasenby (2011) Forward And Backward Reaching IK (FABRIK) with swing-twist joint-limit cones",
      detail_preset::make_sota(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 0.92F, 1.00F, 0.94F }, 0.0F, 0.5F, 0.0F, 1,
                               NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController,
                               "Characters", "FullBodyFABRIK3D + MultiEffectorConstrainedIK") },

    { 232, "VoronoiFractureDestruction3D", "VoronoiFractureDestruction3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Shatters walls, pillars, and crates into physical rigid-body fragments on high-velocity impact!",
      "3D Voronoi cell plane-clipping (`ShatterMesh3D`) + connectivity graph island detection",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 1.2F, 1.2F, 1.2F }, { 0.95F, 0.55F, 0.25F }, 0.30F, 0.35F, 0.0F, 3,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider, "Physics",
                               "VoronoiFractureDestruction3D + VoronoiCells + BrickPBR") },

    { 233, "AeroHydrodynamicsTensor3D", "AeroHydrodynamicsTensor3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Turns any mesh into a glider, aircraft wing, or floating boat without custom force scripts",
      "Angle-of-attack airfoil lift/drag polar tensor + Archimedes submerged-triangle hydrostatic pressure integration",
      detail_preset::make_sota(CatalogPrimKind::Cone, { 1.4F, 0.45F, 1.6F }, { 0.28F, 0.84F, 0.98F }, 0.85F, 0.16F, 0.0F, 4,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider | NodeSpawnFlags::kKinematicMover,
                               "Physics", "AeroHydrodynamicsTensor3D + AirfoilAndBuoyancy") },

    { 234, "UtilityGOAPBrainAI", "UtilityGOAPBrainAI", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Pilot, "42:00+ (Beyond Godot SOTA)",
      "Combines Utility response curves, Goal-Oriented Action Planning (GOAP), and Behavior Trees in one AI Pilot",
      "A* world-state action planner + dual-utility consideration scorer (`cd::editor_panel_behavior_designer`)",
      detail_preset::make_sota(CatalogPrimKind::Cone, { 0.90F, 1.20F, 0.90F }, { 0.96F, 0.32F, 0.86F }, 0.94F, 0.14F, 420.0F, 0,
                               NodeSpawnFlags::kPatrolAi | NodeSpawnFlags::kTriggerHazard | NodeSpawnFlags::kBoxCollider,
                               "Characters", "UtilityGOAPBrainAI + GoapPlanner + SynthPilot") },

    { 235, "RayTracedAudioAcoustics3D", "RayTracedAudioAcoustics3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Automatically computes room reverb, wall occlusion, and doorway diffraction from 3D scene geometry!",
      "BVH stochastic acoustic ray tracing + image-source early reflections + FDN late reverb (`EchoChamber3D`)",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 0.80F, 0.80F, 0.80F }, { 1.00F, 0.76F, 0.28F }, 0.88F, 0.14F, 0.0F, 0,
                               NodeSpawnFlags::kSphereCollider, "Default",
                               "RayTracedAudioAcoustics3D + BvhEarlyReflections + FDNReverb") },

    { 236, "ProceduralModalSynthAudio", "ProceduralModalSynthAudio", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Synthesizes realistic metal, glass, wood, and stone impact chimes from physics contact velocity — zero `.wav` files needed!",
      "Bank of damped harmonic modal resonators driven by `ContactManifold` impulse magnitude (`SplashAudioCue`)",
      detail_preset::make_sota(CatalogPrimKind::Torus, { 0.85F, 0.85F, 0.85F }, { 1.00F, 0.84F, 0.36F }, 0.96F, 0.10F, 0.0F, 0,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider, "Physics",
                               "ProceduralModalSynthAudio + ModalResonatorBank") },

    { 237, "RollbackNetcodeDeterminism", "RollbackNetcodeDeterminism", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::PulseBus, "42:00+ (Beyond Godot SOTA)",
      "Lag-free fighting/action multiplayer with automatic state save, input prediction, and instant rollback resimulation",
      "GGRS-style fixed-point/IEEE-754 deterministic ECS world snapshot ring buffer + frame checksum verifier (`cd::net`)",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 0.95F, 0.95F, 0.95F }, { 0.28F, 0.96F, 0.74F }, 0.80F, 0.18F, 0.0F, 4,
                               NodeSpawnFlags::kBoxCollider, "Gameplay",
                               "RollbackNetcodeDeterminism + SnapshotRing + Resimulate") },

    { 238, "TimeRewindWorldRecorder", "TimeRewindWorldRecorder", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::PulseBus, "42:00+ (Beyond Godot SOTA)",
      "Braid/Prince-of-Persia style live gameplay time-rewind and instant replay scrubbing in the editor!",
      "Delta-compressed transform/velocity/modifier history ring + `cd::game_input_recorder` deterministic replay",
      detail_preset::make_sota(CatalogPrimKind::Cylinder, { 0.85F, 1.10F, 0.85F }, { 0.42F, 0.82F, 1.00F }, 0.90F, 0.12F, 480.0F, 0,
                               NodeSpawnFlags::kKinematicMover | NodeSpawnFlags::kCapsuleCollider, "Gameplay",
                               "TimeRewindWorldRecorder + DeltaTimelineScrub") },

    { 239, "PCGWaveFunctionCollapse3D", "PCGWaveFunctionCollapse3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Generates complete 3D arenas, dungeons, and platforming courses from adjacency rules in 1 click",
      " Shannon-entropy Wave Function Collapse (WFC) constraint propagator with backtracking & seed lock",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 1.6F, 1.0F, 1.6F }, { 0.38F, 0.88F, 0.62F }, 0.35F, 0.28F, 0.0F, 2,
                               NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "Geometry",
                               "PCGWaveFunctionCollapse3D + EntropyWFCGenerator") },

    { 240, "SplineTerrainRoadDeformer3D", "SplineTerrainRoadDeformer3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Carves roads, ramps, and riverbeds into terrain along a 3D Bezier curve with automatic embankment blending",
      "GPU heightmap SDF spline distance field + automatic splatmap weight painting (`cd::terrain`)",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 3.2F, 0.28F, 1.6F }, { 0.88F, 0.68F, 0.24F }, 0.35F, 0.24F, 0.0F, 4,
                               NodeSpawnFlags::kRigidBodyStatic | NodeSpawnFlags::kBoxCollider, "Geometry",
                               "SplineTerrainRoadDeformer3D + SplineCarve + CarbonRoad") },

    { 241, "ScreenSpaceGIHorizon3D", "ScreenSpaceGIHorizon3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Adds contact-hardening ambient occlusion and near-field color bleeding on top of DDGI probes",
      "Ground-Truth Ambient Occlusion (Jimenez et al. 2016 `cd::post_gtao`) + visibility bitmask bent-normal GI",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 1.00F, 1.00F, 1.00F }, { 0.92F, 0.88F, 0.98F }, 0.75F, 0.18F, 0.0F, 5,
                               NodeSpawnFlags::kEnvSkyDdgi | NodeSpawnFlags::kSphereCollider, "Lights",
                               "ScreenSpaceGIHorizon3D + BitmaskGTAO + BentNormals") },

    { 242, "PhysicalCinemaLens3D", "PhysicalCinemaLens3D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::TraitCapsule, "42:00+ (Beyond Godot SOTA)",
      "Real cinema camera controls: f-stop aperture, focal length, anamorphic bokeh, chromatic dispersion, and AgX film look",
      "7-mip Jimenez dual-filter HDR Bloom (`cd::post_bloom`) + Circle-of-Confusion DoF (`cd::post_dof`) + AgX tonemapper",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 0.95F, 0.95F, 0.95F }, { 1.00F, 0.82F, 0.42F }, 1.00F, 0.08F, 380.0F, 0,
                               NodeSpawnFlags::kEnvBloomLens | NodeSpawnFlags::kEnvTonemap | NodeSpawnFlags::kSphereCollider,
                               "Default", "PhysicalCinemaLens3D + Bloom + DoF + AgX") },

    { 243, "ReactiveMVVMDataBindUI", "ReactiveMVVMDataBindUI", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::PulseBus, "42:00+ (Beyond Godot SOTA)",
      "Two-way binds any ECS component or C++23 struct field (`player_hp`, `game_score`) to ChromaUI widgets with zero glue code!",
      "Compile-time pointer-to-member reflection descriptor + dirty-generation reactive `Pulse` notifier",
      detail_preset::make_ui({ 0.30F, 0.94F, 0.78F }, "ReactiveMVVMDataBindUI + TwoWayEcsBinder") },

    { 244, "GPUVectorSlugCanvas2D", "GPUVectorSlugCanvas2D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Renders SVG icons, HUD reticles, and curved diagrams at 4K/8K with infinite zoom sharpness",
      " Lengyel Slug-style GPU fragment quadratic/cubic Bezier winding-number evaluator + `ChromaUi::DrawIcon`",
      detail_preset::make_2d(CatalogPrimKind::Torus, { 0.32F, 0.92F, 1.00F }, NodeSpawnFlags::kChromaUiBinding, "GPUVectorSlugCanvas2D + GpuBezierEvaluator") },

    { 245, "VerletRopeAndBridge2D", "VerletRopeAndBridge2D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Vessel, "42:00+ (Beyond Godot SOTA)",
      "1-click suspension bridges, grappling hooks, and hanging vines that sag under player weight!",
      "16-substep Jacobi/Gauss-Seidel Verlet distance constraint chain with two-way rigid-body impulse coupling",
      detail_preset::make_2d(CatalogPrimKind::Cylinder, { 0.92F, 0.72F, 0.36F }, NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kBoxCollider, "VerletRopeAndBridge2D + 16SubstepVerlet", 6) },

    { 246, "GPUCellularAutomata2D", "GPUCellularAutomata2D", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Noita-style interactive falling sand, flowing water, rising smoke, and spreading fire simulation grid",
      "Checkerboard-pass ping-pong compute cellular automata + march-squares dynamic collider extraction",
      detail_preset::make_2d(CatalogPrimKind::Cube, { 1.00F, 0.58F, 0.22F }, NodeSpawnFlags::kTriggerHazard | NodeSpawnFlags::kBoxCollider, "GPUCellularAutomata2D + CheckerboardCompute") },

    { 247, "LiveSPIRVShaderGraphNode", "LiveSPIRVShaderGraphNode", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Element, "42:00+ (Beyond Godot SOTA)",
      "Edits visual shader nodes or `.glsl` files with zero-stutter background compilation and instant viewport hot-swap",
      "`cd::gluon` AST-to-SPIR-V compiler + frame-fence `DeferredDestroyQueue` pipeline swap (`Wave 3.1`)",
      detail_preset::make_sota(CatalogPrimKind::Sphere, { 1.05F, 1.05F, 1.05F }, { 0.88F, 0.42F, 0.98F }, 0.96F, 0.10F, 480.0F, 0,
                               NodeSpawnFlags::kSphereCollider | NodeSpawnFlags::kChromaUiBinding, "Default",
                               "LiveSPIRVShaderGraphNode + HitchFreePipelineSwap") },

    { 248, "DeterministicBotVerifier", "DeterministicBotVerifier", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::Pilot, "42:00+ (Beyond Godot SOTA)",
      "1-click `[>] Auto-Play E2E (P)` bot that plays the entire level, fires at enemies, collects all cores, and reaches the goal!",
      "Closed-loop hazard/platform/target utility autopilot (`w.autopilot_play_active`) + Golden Fixture #5 byte-parity verifier",
      detail_preset::make_sota(CatalogPrimKind::GltfAvatar, { 1.8F, 1.8F, 1.8F }, { 0.35F, 1.00F, 0.65F }, 0.0F, 0.5F, 0.0F, 1,
                               NodeSpawnFlags::kRigidBodyDynamic | NodeSpawnFlags::kCapsuleCollider | NodeSpawnFlags::kCharacterController,
                               "Characters", "DeterministicBotVerifier + AutopilotE2E") },

    { 249, "ECSArchetypeQueryBus", "ECSArchetypeQueryBus", StudioNodeCategory::NextGenSOTA, ChromaConstructKind::PulseBus, "42:00+ (Beyond Godot SOTA)",
      "Processes 100,000+ entities in parallel across all CPU cores with zero heap allocations per frame",
      "EnTT/Bevy-style SoA archetype chunk iteration (`cd::ecs` + `cd::concurrency` work-stealing job dispatcher)",
      detail_preset::make_sota(CatalogPrimKind::Cube, { 1.0F, 1.0F, 1.0F }, { 0.32F, 0.88F, 0.98F }, 0.90F, 0.14F, 0.0F, 4,
                               NodeSpawnFlags::kBoxCollider, "Default",
                               "ECSArchetypeQueryBus + ParallelSoADispatcher") },
}};

// =============================================================================
// Compile-Time & Runtime Query Helpers
// =============================================================================
[[nodiscard]] constexpr std::size_t count_studio_nodes_in_category(StudioNodeCategory cat) noexcept
{
    std::size_t n = 0;
    for (const auto& d : kStudioNodeCatalog)
    {
        if (d.category == cat) ++n;
    }
    return n;
}

[[nodiscard]] constexpr std::size_t count_godot_video_nodes() noexcept
{
    std::size_t n = 0;
    for (const auto& d : kStudioNodeCatalog)
    {
        if (d.category != StudioNodeCategory::NextGenSOTA) ++n;
    }
    return n;
}

[[nodiscard]] constexpr std::size_t count_next_gen_sota_nodes() noexcept
{
    return count_studio_nodes_in_category(StudioNodeCategory::NextGenSOTA);
}

static_assert(count_studio_nodes_in_category(StudioNodeCategory::CoreSystem)  == kGodotBaseNodeCount);
static_assert(count_studio_nodes_in_category(StudioNodeCategory::Node2D)      == kGodot2DNodeCount);
static_assert(count_studio_nodes_in_category(StudioNodeCategory::Node3D)      == kGodot3DNodeCount);
static_assert(count_studio_nodes_in_category(StudioNodeCategory::ControlUI)   == kGodotUINodeCount);
static_assert(count_studio_nodes_in_category(StudioNodeCategory::NextGenSOTA) == kNextGenSotaNodeCount);
static_assert(count_godot_video_nodes()   == kGodotTotalNodeCount);
static_assert(count_next_gen_sota_nodes() == kNextGenSotaNodeCount);

[[nodiscard]] inline bool catalog_str_equals_icase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

[[nodiscard]] inline bool catalog_str_contains_icase(std::string_view hay, std::string_view needle) noexcept
{
    if (needle.empty()) return true;
    if (hay.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
    {
        if (catalog_str_equals_icase(hay.substr(i, needle.size()), needle))
            return true;
    }
    return false;
}

[[nodiscard]] constexpr const StudioNodeDescriptor* find_studio_node_by_id(std::uint16_t id) noexcept
{
    if (id < kStudioNodeCatalog.size())
        return &kStudioNodeCatalog[id];
    return nullptr;
}

inline constexpr const auto& kStudioUniversalNodeCatalog = kStudioNodeCatalog;

[[nodiscard]] inline const StudioNodeDescriptor* find_studio_node_by_godot_name(std::string_view name) noexcept
{
    if (name.empty()) return nullptr;
    for (const auto& d : kStudioNodeCatalog)
    {
        if (std::string_view(d.godot_name) == name)
            return &d;
    }
    for (const auto& d : kStudioNodeCatalog)
    {
        if (catalog_str_equals_icase(d.godot_name, name))
            return &d;
    }
    if (catalog_str_equals_icase(name, "SubViewportContainer"))
    {
        return &kStudioNodeCatalog[2]; // SubViewport / NestedStagePortal
    }
    return nullptr;
}

[[nodiscard]] inline const StudioNodeDescriptor* find_studio_node_by_chroma_name(std::string_view name) noexcept
{
    if (name.empty()) return nullptr;
    for (const auto& d : kStudioNodeCatalog)
    {
        if (std::string_view(d.chroma_name) == name)
            return &d;
    }
    for (const auto& d : kStudioNodeCatalog)
    {
        if (catalog_str_equals_icase(d.chroma_name, name))
            return &d;
    }
    return nullptr;
}

[[nodiscard]] inline const StudioNodeDescriptor* find_studio_node(std::string_view query) noexcept
{
    if (query.empty()) return nullptr;
    if (const auto* exact_g = find_studio_node_by_godot_name(query)) return exact_g;
    if (const auto* exact_c = find_studio_node_by_chroma_name(query)) return exact_c;
    for (const auto& d : kStudioNodeCatalog)
    {
        if (catalog_str_contains_icase(d.godot_name, query) ||
            catalog_str_contains_icase(d.chroma_name, query))
        {
            return &d;
        }
    }
    return nullptr;
}

[[nodiscard]] inline bool studio_node_matches_filter(
    const StudioNodeDescriptor& d,
    int                         category_filter,
    std::string_view            search_query) noexcept
{
    if (category_filter >= 0 && category_filter <= 4)
    {
        if (static_cast<int>(d.category) != category_filter)
            return false;
    }
    if (search_query.empty())
        return true;
    return catalog_str_contains_icase(d.godot_name, search_query) ||
           catalog_str_contains_icase(d.chroma_name, search_query) ||
           catalog_str_contains_icase(d.ux_upgrade, search_query) ||
           catalog_str_contains_icase(d.tech_upgrade, search_query) ||
           catalog_str_contains_icase(d.spawn_preset.bundled_summary, search_query) ||
           (catalog_str_contains_icase("SubViewportContainer", search_query) && d.id == 2);
}

[[nodiscard]] inline std::vector<const StudioNodeDescriptor*> filter_studio_nodes(
    int              category_filter = -1,
    std::string_view search_query    = {})
{
    std::vector<const StudioNodeDescriptor*> out;
    out.reserve(kTotalStudioNodeCount);
    for (const auto& d : kStudioNodeCatalog)
    {
        if (studio_node_matches_filter(d, category_filter, search_query))
            out.push_back(&d);
    }
    return out;
}

// =============================================================================
// Lightweight Headless-Testable `StudioState` for Unit Verification & Scripting
// =============================================================================
struct StudioCatalogEntityRecord
{
    std::uint32_t       entity_id         { 0 };
    std::int32_t        parent_entity_idx { -1 };
    std::uint16_t       catalog_node_id   { 0 };
    std::string         name              {};
    std::string         godot_name        {};
    std::string         chroma_name       {};
    StudioNodeCategory  category          { StudioNodeCategory::CoreSystem };
    ChromaConstructKind construct_kind    { ChromaConstructKind::Element };
    CatalogPrimKind     prim_kind         { CatalogPrimKind::Cube };
    cd::math::Vec3f     position          { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f     scale             { 1.0F, 1.0F, 1.0F };
    cd::math::Vec3f     albedo            { 1.0F, 1.0F, 1.0F };
    float               metallic          { 0.0F };
    float               roughness         { 0.5F };
    float               multiscatter_comp { 1.0F };
    float               iridescence_nm    { 0.0F };
    int                 texture_slot      { 0 };
    std::uint32_t       modifier_flags    { 0 };
    std::string         layer             { "Default" };
    std::vector<std::uint16_t> folded_capsule_ids {};
};

struct StudioState
{
    std::vector<StudioCatalogEntityRecord> entities          {};
    int                                    selected_idx      { -1 };
    std::uint32_t                          active_env_flags  { 0 };
    std::uint32_t                          spawned_lights    { 0 };
    std::uint32_t                          planar_2d_count   { 0 };
    std::uint32_t                          chroma_ui_count   { 0 };
    std::uint32_t                          sota_compute_count{ 0 };
    std::string                            last_log_message  {};
};

/// Spawn as root, nest as child, or fold as inline capsule onto a `StudioState`.
[[nodiscard]] inline bool apply_catalog_node_to_studio_state(
    StudioState&       state,
    std::uint16_t      node_id,
    CatalogComposeMode mode = CatalogComposeMode::SpawnRootElement)
{
    const auto* desc = find_studio_node_by_id(node_id);
    if (desc == nullptr)
        return false;

    const auto& p = desc->spawn_preset;

    // Update global environment / light / subsystem counters
    if ((p.modifier_flags & (NodeSpawnFlags::kEnvFog | NodeSpawnFlags::kEnvBloomLens |
                             NodeSpawnFlags::kEnvTonemap | NodeSpawnFlags::kEnvSkyDdgi)) != 0u)
    {
        state.active_env_flags |= p.modifier_flags;
    }
    if ((p.modifier_flags & (NodeSpawnFlags::kLightPoint | NodeSpawnFlags::kLightSpot |
                             NodeSpawnFlags::kLightDirectional)) != 0u)
    {
        ++state.spawned_lights;
    }
    if ((p.modifier_flags & NodeSpawnFlags::kPlanar2DBinding) != 0u)
        ++state.planar_2d_count;
    if ((p.modifier_flags & NodeSpawnFlags::kChromaUiBinding) != 0u)
        ++state.chroma_ui_count;
    if ((p.modifier_flags & NodeSpawnFlags::kSotaGpuCompute) != 0u)
        ++state.sota_compute_count;

    if (mode == CatalogComposeMode::FoldAsInlineCapsule &&
        state.selected_idx >= 0 &&
        static_cast<std::size_t>(state.selected_idx) < state.entities.size())
    {
        auto& target = state.entities[static_cast<std::size_t>(state.selected_idx)];
        target.modifier_flags |= p.modifier_flags;
        if (std::find(target.folded_capsule_ids.begin(), target.folded_capsule_ids.end(), desc->id) ==
            target.folded_capsule_ids.end())
        {
            target.folded_capsule_ids.push_back(desc->id);
        }
        if (p.iridescence_nm > 0.0F)
            target.iridescence_nm = p.iridescence_nm;
        state.last_log_message = std::string("[capsule] Folded inline capsule '") + desc->chroma_name +
                                 "' (" + desc->godot_name + ") into '" + target.name + "'";
        return true;
    }

    StudioCatalogEntityRecord rec {};
    rec.entity_id         = static_cast<std::uint32_t>(state.entities.size() + 1U);
    rec.parent_entity_idx = (mode == CatalogComposeMode::NestAsChildElement &&
                             state.selected_idx >= 0 &&
                             static_cast<std::size_t>(state.selected_idx) < state.entities.size())
                                ? state.selected_idx
                                : -1;
    rec.catalog_node_id   = desc->id;
    rec.name              = std::string(desc->chroma_name) + "_" + std::to_string(rec.entity_id);
    rec.godot_name        = desc->godot_name;
    rec.chroma_name       = desc->chroma_name;
    rec.category          = desc->category;
    rec.construct_kind    = desc->construct_kind;
    rec.prim_kind         = p.prim_kind;
    rec.position          = (rec.parent_entity_idx >= 0)
                                ? cd::math::Vec3f { 0.0F, 1.15F, 0.0F }
                                : cd::math::Vec3f { static_cast<float>(state.entities.size() % 10U) * 1.5F, 0.5F, 0.0F };
    rec.scale             = p.scale;
    rec.albedo            = p.albedo;
    rec.metallic          = p.metallic;
    rec.roughness         = p.roughness;
    rec.multiscatter_comp = p.multiscatter_comp;
    rec.iridescence_nm    = p.iridescence_nm;
    rec.texture_slot      = p.texture_slot;
    rec.modifier_flags    = p.modifier_flags;
    rec.layer             = p.semantic_layer != nullptr ? p.semantic_layer : "Default";
    rec.folded_capsule_ids.push_back(desc->id);

    state.entities.push_back(std::move(rec));
    state.selected_idx = static_cast<int>(state.entities.size()) - 1;
    state.last_log_message = std::string("[catalog] Spawned '") + desc->chroma_name +
                             "' (Godot: " + desc->godot_name + ")";
    return true;
}

[[nodiscard]] inline std::uint32_t studio_state_entity_depth(
    const StudioState& state,
    std::size_t        entity_idx) noexcept
{
    std::uint32_t depth = 0U;
    std::size_t   cur   = entity_idx;
    while (cur < state.entities.size() && depth < 64U)
    {
        const std::int32_t p = state.entities[cur].parent_entity_idx;
        if (p < 0 || static_cast<std::size_t>(p) >= state.entities.size() || static_cast<std::size_t>(p) == cur)
            break;
        ++depth;
        cur = static_cast<std::size_t>(p);
    }
    return depth;
}

[[nodiscard]] inline bool has_folded_capsule(
    const StudioCatalogEntityRecord& rec,
    std::uint16_t                    node_id) noexcept
{
    return std::find(rec.folded_capsule_ids.begin(), rec.folded_capsule_ids.end(), node_id) !=
           rec.folded_capsule_ids.end();
}

[[nodiscard]] inline std::string serialize_studio_state_json(
    const StudioState& state,
    bool               pretty = false)
{
    (void)pretty;
    std::ostringstream os;
    os << "{\"format\":\"chromodynamic.studio.catalog_state.v1\""
       << ",\"selected_idx\":" << state.selected_idx
       << ",\"active_env_flags\":" << state.active_env_flags
       << ",\"spawned_lights\":" << state.spawned_lights
       << ",\"planar_2d_count\":" << state.planar_2d_count
       << ",\"chroma_ui_count\":" << state.chroma_ui_count
       << ",\"sota_compute_count\":" << state.sota_compute_count
       << ",\"entities\":[";
    for (std::size_t i = 0; i < state.entities.size(); ++i)
    {
        if (i > 0U) os << ",";
        const auto& e = state.entities[i];
        os << "{\"entity_id\":" << e.entity_id
           << ",\"parent_entity_idx\":" << e.parent_entity_idx
           << ",\"catalog_node_id\":" << e.catalog_node_id
           << ",\"name\":\"" << e.name << "\""
           << ",\"godot_name\":\"" << e.godot_name << "\""
           << ",\"chroma_name\":\"" << e.chroma_name << "\""
           << ",\"layer\":\"" << e.layer << "\""
           << ",\"modifier_flags\":" << e.modifier_flags
           << ",\"metallic\":" << e.metallic
           << ",\"roughness\":" << e.roughness
           << ",\"multiscatter_comp\":" << e.multiscatter_comp
           << ",\"iridescence_nm\":" << e.iridescence_nm
           << ",\"texture_slot\":" << e.texture_slot
           << ",\"position\":[" << e.position.x << "," << e.position.y << "," << e.position.z << "]"
           << ",\"scale\":[" << e.scale.x << "," << e.scale.y << "," << e.scale.z << "]"
           << ",\"albedo\":[" << e.albedo.x << "," << e.albedo.y << "," << e.albedo.z << "]"
           << ",\"folded_capsule_ids\":[";
        for (std::size_t ci = 0; ci < e.folded_capsule_ids.size(); ++ci)
        {
            if (ci > 0U) os << ",";
            os << e.folded_capsule_ids[ci];
        }
        os << "]}";
    }
    os << "]}";
    return os.str();
}

[[nodiscard]] inline bool deserialize_studio_state_json(
    std::string_view json_text,
    StudioState&     out_state)
{
    if (json_text.find("\"chromodynamic.studio.catalog_state.v1\"") == std::string_view::npos)
        return false;

    auto read_num = [](std::string_view src, std::string_view key, double def_val = 0.0) -> double {
        const auto pos = src.find(key);
        if (pos == std::string_view::npos) return def_val;
        const auto colon = src.find(':', pos + key.size());
        if (colon == std::string_view::npos) return def_val;
        const std::string sub(src.substr(colon + 1U, 48U));
        return std::strtod(sub.c_str(), nullptr);
    };
    auto read_str = [](std::string_view src, std::string_view key) -> std::string {
        const auto pos = src.find(key);
        if (pos == std::string_view::npos) return {};
        const auto q1 = src.find('"', pos + key.size());
        if (q1 == std::string_view::npos) return {};
        const auto q2 = src.find('"', q1 + 1U);
        if (q2 == std::string_view::npos) return {};
        return std::string(src.substr(q1 + 1U, q2 - q1 - 1U));
    };
    auto read_vec3 = [](std::string_view src, std::string_view key, cd::math::Vec3f& v) {
        const auto pos = src.find(key);
        if (pos == std::string_view::npos) return;
        const auto b1 = src.find('[', pos + key.size());
        const auto b2 = src.find(']', b1 != std::string_view::npos ? b1 : 0U);
        if (b1 == std::string_view::npos || b2 == std::string_view::npos || b2 <= b1) return;
        const std::string sub(src.substr(b1 + 1U, b2 - b1 - 1U));
        char* end1 = nullptr;
        v.x = std::strtof(sub.c_str(), &end1);
        if (end1 && *end1 == ',')
        {
            char* end2 = nullptr;
            v.y = std::strtof(end1 + 1, &end2);
            if (end2 && *end2 == ',')
                v.z = std::strtof(end2 + 1, nullptr);
        }
    };

    StudioState loaded {};
    loaded.selected_idx       = static_cast<int>(read_num(json_text, "\"selected_idx\""));
    loaded.active_env_flags   = static_cast<std::uint32_t>(read_num(json_text, "\"active_env_flags\""));
    loaded.spawned_lights     = static_cast<std::uint32_t>(read_num(json_text, "\"spawned_lights\""));
    loaded.planar_2d_count    = static_cast<std::uint32_t>(read_num(json_text, "\"planar_2d_count\""));
    loaded.chroma_ui_count    = static_cast<std::uint32_t>(read_num(json_text, "\"chroma_ui_count\""));
    loaded.sota_compute_count = static_cast<std::uint32_t>(read_num(json_text, "\"sota_compute_count\""));

    const auto ents_pos = json_text.find("\"entities\":[");
    if (ents_pos != std::string_view::npos)
    {
        std::size_t cur = ents_pos + 12U;
        while (cur < json_text.size())
        {
            const auto obj_start = json_text.find("{\"entity_id\":", cur);
            if (obj_start == std::string_view::npos) break;
            const auto obj_end = json_text.find("]}", obj_start);
            if (obj_end == std::string_view::npos) break;
            const std::string_view eo = json_text.substr(obj_start, obj_end + 2U - obj_start);

            StudioCatalogEntityRecord rec {};
            rec.entity_id         = static_cast<std::uint32_t>(read_num(eo, "\"entity_id\""));
            rec.parent_entity_idx = static_cast<std::int32_t>(read_num(eo, "\"parent_entity_idx\"", -1.0));
            rec.catalog_node_id   = static_cast<std::uint16_t>(read_num(eo, "\"catalog_node_id\""));
            rec.name              = read_str(eo, "\"name\"");
            rec.godot_name        = read_str(eo, "\"godot_name\"");
            rec.chroma_name       = read_str(eo, "\"chroma_name\"");
            rec.layer             = read_str(eo, "\"layer\"");
            rec.modifier_flags    = static_cast<std::uint32_t>(read_num(eo, "\"modifier_flags\""));
            rec.metallic          = static_cast<float>(read_num(eo, "\"metallic\""));
            rec.roughness         = static_cast<float>(read_num(eo, "\"roughness\""));
            rec.multiscatter_comp = static_cast<float>(read_num(eo, "\"multiscatter_comp\""));
            rec.iridescence_nm    = static_cast<float>(read_num(eo, "\"iridescence_nm\""));
            rec.texture_slot      = static_cast<int>(read_num(eo, "\"texture_slot\""));
            read_vec3(eo, "\"position\"", rec.position);
            read_vec3(eo, "\"scale\"", rec.scale);
            read_vec3(eo, "\"albedo\"", rec.albedo);

            if (const auto* desc = find_studio_node_by_id(rec.catalog_node_id))
            {
                rec.category       = desc->category;
                rec.construct_kind = desc->construct_kind;
                rec.prim_kind      = desc->spawn_preset.prim_kind;
            }

            const auto fc_pos = eo.find("\"folded_capsule_ids\":[");
            if (fc_pos != std::string_view::npos)
            {
                const auto fc_end = eo.find(']', fc_pos + 22U);
                if (fc_end != std::string_view::npos)
                {
                    const std::string fc_str(eo.substr(fc_pos + 22U, fc_end - (fc_pos + 22U)));
                    const char* p = fc_str.c_str();
                    while (*p != '\0')
                    {
                        char* next_p = nullptr;
                        const long v = std::strtol(p, &next_p, 10);
                        if (next_p == p) break;
                        rec.folded_capsule_ids.push_back(static_cast<std::uint16_t>(v));
                        p = next_p;
                        if (*p == ',') ++p;
                    }
                }
            }

            loaded.entities.push_back(std::move(rec));
            cur = obj_end + 2U;
        }
    }

    out_state = std::move(loaded);
    return true;
}

} // namespace cd::studio
