// =============================================================================
// CHROMODYNAMIC — cd/editor/FullFledgedEditorSuite.cpp
// FAZ 7 (Waves 7.1, 7.2, 7.3) — Full-Fledged 100% cd::ui Editor & PIE Implementation.
// =============================================================================
#include <cd/editor/FullFledgedEditorSuite.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace cd::editor
{

// =============================================================================
// Wave 7.1 — EditorCockpitShell
// =============================================================================

EditorCockpitShell::EditorCockpitShell()
{
    panel_ids_ = {
        "scene_tree",
        "scene_navigator",
        "viewport",
        "inspector",
        "settings_panel",
        "scene_palette",
        "console",
        "assets",
        "asset_drop_target",
        "build_panel",
        "material_editor",
        "vehicle_editor",
        "material_preview",
        "cutscene_player",
        "dialog_tree_editor",
        "pathfinding_viz",
        "behavior_designer",
        "light_editor",
        "perf_profiler",
        "asset_pipeline_status",
        "lobby_browser",
        "animator",
        "ik_chain_editor",
        "input_recorder",
        "debug_viz",
        "auto_save_indicator",
        "status_bar_fps_sparkline",
    };

    main_splitter_.set_rect({ 0.0F, 0.0F, 1600.0F, 900.0F });

    showcases_.reserve(cd::gameplay_director::kShowcaseCount);
    for (std::size_t i = 0; i < cd::gameplay_director::kShowcaseCount; ++i)
    {
        const auto id = static_cast<cd::gameplay_director::ShowcaseId>(i);
        const std::string slug { cd::gameplay_director::showcase_slug(id) };
        showcases_.push_back(ShowcaseProjectEntry {
            id,
            slug,
            "showcases/" + slug + "/" + slug + ".cdproj",
            "showcases/" + slug + "/main.cdstage"
        });
    }
}

cd::element_core::Stage EditorCockpitShell::open_showcase_in_editor(
    cd::gameplay_director::ShowcaseId id
) const
{
    return cd::gameplay_director::build_showcase_stage(id);
}

// =============================================================================
// Wave 7.2 — AddElementCatalog, ElementInspectorBinder, Gizmo, TerrainFoliageBrush
// =============================================================================

AddElementCatalog::AddElementCatalog()
{
    items_ = {
        // Core
        { "Element", "Core", "Base CHROMODYNAMIC scene element" },
        { "Planar2D", "Core", "2D spatial element" },
        { "Spatial3D", "Core", "3D spatial element with Mobility" },
        // Visual
        { "SurfaceMesh3D", "Visual", "Static/dynamic PBR mesh surface" },
        { "RiggedMesh3D", "Visual", "GPU dual-quat skinned character mesh" },
        { "SwarmMesh3D", "Visual", "GPU multi-draw indirect instanced swarm" },
        { "ProjectionDecal3D", "Visual", "Deferred G-buffer box decal" },
        { "SparkSwarm3D", "Visual", "Depth-colliding GPU particle swarm" },
        { "CameraLens3D", "Visual", "Physical camera lens with DoF/Exposure" },
        { "SpringArm3D", "Visual", "Collision-aware camera boom arm" },
        // Radiance
        { "SolarLight3D", "Radiance", "Physical Lux/Kelvin directional sun/moon light" },
        { "RadialLight3D", "Radiance", "Lumen omnidirectional point light" },
        { "ConeLight3D", "Radiance", "IES photometric spot light" },
        { "PanelLight3D", "Radiance", "LTC polygonal area light" },
        { "RadianceProbe3D", "Radiance", "Local parallax-corrected IBL probe" },
        { "IrradianceField3D", "Radiance", "DDGI + ReSTIR GI + NRC probe volume" },
        // Aether & Optics
        { "CelestialDome3D", "Aether", "Hillaire sky-atmosphere LUT dome" },
        { "AetherFog3D", "Aether", "160x90x128 froxel volumetric fog" },
        { "FogPocket3D", "Aether", "Localized density/albedo fog volume" },
        { "CloudStrata3D", "Aether", "Raymarched volumetric cloud layer" },
        { "SunBeams3D", "Aether", "Crepuscular radial light shafts" },
        { "OpticsVolume3D", "Optics", "PostFX volume (GTAO/SSR/TAA/Bloom/DoF/AgX)" },
        // Nature
        { "TerraClipmap3D", "Nature", "GPU clipmap terrain & splatmap" },
        { "OceanWave3D", "Nature", "Tessendorf FFT ocean wave surface" },
        // Dynamics
        { "AnchorBody3D", "Dynamics", "Jolt static/kinematic physics body" },
        { "KineticBody3D", "Dynamics", "Jolt dynamic rigid body" },
        { "ClothSurface3D", "Dynamics", "XPBD cloth simulation surface" },
        { "ShatterMesh3D", "Dynamics", "Voronoi fracturable destruction mesh" },
        { "RagdollChain3D", "Dynamics", "Articulated ragdoll chain" },
        { "SensorVolume3D", "Dynamics", "Overlap/enter/exit trigger volume" },
        { "CollisionHull3D", "Dynamics", "Collision shape hull" },
        { "RayProbe3D", "Dynamics", "Continuous physics raycast sensor" },
        // Acoustics & Wayfinding
        { "SoundSource3D", "Acoustics", "MIT KEMAR HRTF 3D spatial sound source" },
        { "AudioEar3D", "Acoustics", "3D acoustic listener" },
        { "EchoChamber3D", "Acoustics", "Reverb/occlusion acoustic zone" },
        { "NavMeshZone3D", "Wayfinding", "Navigation mesh bake region" },
        { "WayfinderAgent3D", "Wayfinding", "A* + RVO crowd steering agent" },
        { "SplineTrack3D", "Wayfinding", "Catmull-Rom / Bezier 3D path" },
        // Planar 2D
        { "QuadSprite2D", "Planar2D", "2D sprite quad" },
        { "Flipbook2D", "Planar2D", "2D frame-animated sprite" },
        { "TileCanvas2D", "Planar2D", "Multi-layer 2D tilemap canvas" },
        { "Lens2D", "Planar2D", "2D camera with deadzone & parallax" },
        { "KineticBody2D", "Planar2D", "2D physics body" },
        { "SensorZone2D", "Planar2D", "2D trigger zone" },
        { "RadialLight2D", "Planar2D", "2D normal-mapped point light" },
        // UI
        { "HudLayer", "UI", "Root cd::ui canvas overlay" },
        { "UiFrame", "UI", "Flex/Grid layout container" },
        { "UiCommandButton", "UI", "Interactive button widget" },
        { "UiValueSlider", "UI", "Numeric value slider" },
        { "UiVectorBox", "UI", "X/Y/Z vector scrub box" },
        { "UiGlyphText", "UI", "UTF-8/Turkish/RTL SDF text" },
        { "UiGaugeBar", "UI", "Health/stamina/progress gauge bar" },
        // Gameplay Framework
        { "Vessel", "Gameplay", "Base possessable gameplay vessel" },
        { "Avatar", "Gameplay", "Humanoid character vessel with Locomotion + IK + Camera" },
        { "RoverVessel", "Gameplay", "4+ wheel drivable vehicle vessel" },
        { "HumanPilot", "Gameplay", "Local player input & camera brain pilot" },
        { "SynthPilot", "Gameplay", "AI BehaviorTree + Wayfinder + Squad pilot" },
        { "StageDirector", "Gameplay", "Match rules & SpawnBeacon orchestrator" },
        { "WorldLedger", "Gameplay", "Replicated world state & score ledger" },
        { "SpawnBeacon3D", "Gameplay", "Prioritized team spawn point" },
    };
}

std::vector<ElementCatalogItem> AddElementCatalog::filter_by_category(std::string_view category) const
{
    std::vector<ElementCatalogItem> out;
    for (const auto& item : items_)
    {
        if (item.category == category)
            out.push_back(item);
    }
    return out;
}

std::vector<ElementCatalogItem> AddElementCatalog::search(std::string_view query) const
{
    if (query.empty())
        return items_;
    std::vector<ElementCatalogItem> out;
    for (const auto& item : items_)
    {
        if (item.class_name.find(query) != std::string::npos ||
            item.category.find(query) != std::string::npos)
        {
            out.push_back(item);
        }
    }
    return out;
}

cd::element_core::Element* AddElementCatalog::spawn_into_stage(
    cd::element_core::Stage& stage,
    std::string_view class_name,
    std::string_view instance_name,
    cd::element_core::Element* parent
) const
{
    auto& target_parent = (parent != nullptr) ? *parent : stage.root();
    const std::string name_str = instance_name.empty()
        ? std::string { class_name }
        : std::string { instance_name };

    if (class_name == "SurfaceMesh3D") return &target_parent.emplace_child<cd::elements_visual::SurfaceMesh3D>(name_str);
    if (class_name == "RiggedMesh3D") return &target_parent.emplace_child<cd::elements_visual::RiggedMesh3D>(name_str);
    if (class_name == "SwarmMesh3D") return &target_parent.emplace_child<cd::elements_visual::SwarmMesh3D>(name_str);
    if (class_name == "ProjectionDecal3D") return &target_parent.emplace_child<cd::elements_visual::ProjectionDecal3D>(name_str);
    if (class_name == "SparkSwarm3D") return &target_parent.emplace_child<cd::elements_visual::SparkSwarm3D>(name_str);
    if (class_name == "CameraLens3D") return &target_parent.emplace_child<cd::elements_visual::CameraLens3D>(name_str);
    if (class_name == "SpringArm3D") return &target_parent.emplace_child<cd::elements_visual::SpringArm3D>(name_str);

    if (class_name == "SolarLight3D") return &target_parent.emplace_child<cd::elements_radiance::SolarLight3D>(name_str);
    if (class_name == "RadialLight3D") return &target_parent.emplace_child<cd::elements_radiance::RadialLight3D>(name_str);
    if (class_name == "ConeLight3D") return &target_parent.emplace_child<cd::elements_radiance::ConeLight3D>(name_str);
    if (class_name == "PanelLight3D") return &target_parent.emplace_child<cd::elements_radiance::PanelLight3D>(name_str);
    if (class_name == "RadianceProbe3D") return &target_parent.emplace_child<cd::elements_radiance::RadianceProbe3D>(name_str);
    if (class_name == "IrradianceField3D") return &target_parent.emplace_child<cd::elements_radiance::IrradianceField3D>(name_str);

    if (class_name == "CelestialDome3D") return &target_parent.emplace_child<cd::elements_aether::CelestialDome3D>(name_str);
    if (class_name == "AetherFog3D") return &target_parent.emplace_child<cd::elements_aether::AetherFog3D>(name_str);
    if (class_name == "FogPocket3D") return &target_parent.emplace_child<cd::elements_aether::FogPocket3D>(name_str);
    if (class_name == "CloudStrata3D") return &target_parent.emplace_child<cd::elements_aether::CloudStrata3D>(name_str);
    if (class_name == "SunBeams3D") return &target_parent.emplace_child<cd::elements_aether::SunBeams3D>(name_str);
    if (class_name == "OpticsVolume3D") return &target_parent.emplace_child<cd::elements_optics::OpticsVolume3D>(name_str);

    if (class_name == "TerraClipmap3D") return &target_parent.emplace_child<cd::elements_nature::TerraClipmap3D>(name_str);
    if (class_name == "OceanWave3D") return &target_parent.emplace_child<cd::elements_nature::OceanWave3D>(name_str);

    if (class_name == "AnchorBody3D") return &target_parent.emplace_child<cd::elements_dynamics::AnchorBody3D>(name_str);
    if (class_name == "KineticBody3D") return &target_parent.emplace_child<cd::elements_dynamics::KineticBody3D>(name_str);
    if (class_name == "ClothSurface3D") return &target_parent.emplace_child<cd::elements_dynamics::ClothSurface3D>(name_str);
    if (class_name == "ShatterMesh3D") return &target_parent.emplace_child<cd::elements_dynamics::ShatterMesh3D>(name_str);
    if (class_name == "RagdollChain3D") return &target_parent.emplace_child<cd::elements_dynamics::RagdollChain3D>(name_str);
    if (class_name == "SensorVolume3D") return &target_parent.emplace_child<cd::elements_dynamics::SensorVolume3D>(name_str);
    if (class_name == "CollisionHull3D") return &target_parent.emplace_child<cd::elements_dynamics::CollisionHull3D>(name_str);
    if (class_name == "RayProbe3D") return &target_parent.emplace_child<cd::elements_dynamics::RayProbe3D>(name_str);

    if (class_name == "SoundSource3D") return &target_parent.emplace_child<cd::elements_acoustics::SoundSource3D>(name_str);
    if (class_name == "AudioEar3D") return &target_parent.emplace_child<cd::elements_acoustics::AudioEar3D>(name_str);
    if (class_name == "EchoChamber3D") return &target_parent.emplace_child<cd::elements_acoustics::EchoChamber3D>(name_str);
    if (class_name == "NavMeshZone3D") return &target_parent.emplace_child<cd::elements_wayfinding::NavMeshZone3D>(name_str);
    if (class_name == "WayfinderAgent3D") return &target_parent.emplace_child<cd::elements_wayfinding::WayfinderAgent3D>(name_str);
    if (class_name == "SplineTrack3D") return &target_parent.emplace_child<cd::elements_wayfinding::SplineTrack3D>(name_str);

    if (class_name == "QuadSprite2D") return &target_parent.emplace_child<cd::elements_planar::QuadSprite2D>(name_str);
    if (class_name == "Flipbook2D") return &target_parent.emplace_child<cd::elements_planar::Flipbook2D>(name_str);
    if (class_name == "TileCanvas2D") return &target_parent.emplace_child<cd::elements_planar::TileCanvas2D>(name_str);
    if (class_name == "Lens2D") return &target_parent.emplace_child<cd::elements_planar::Lens2D>(name_str);
    if (class_name == "KineticBody2D") return &target_parent.emplace_child<cd::elements_planar::KineticBody2D>(name_str);
    if (class_name == "SensorZone2D") return &target_parent.emplace_child<cd::elements_planar::SensorZone2D>(name_str);
    if (class_name == "RadialLight2D") return &target_parent.emplace_child<cd::elements_planar::RadialLight2D>(name_str);

    if (class_name == "HudLayer") return &target_parent.emplace_child<cd::elements_ui::HudLayer>(name_str);
    if (class_name == "UiFrame") return &target_parent.emplace_child<cd::elements_ui::UiFrame>(name_str);
    if (class_name == "UiCommandButton") return &target_parent.emplace_child<cd::elements_ui::UiCommandButton>(name_str);
    if (class_name == "UiValueSlider") return &target_parent.emplace_child<cd::elements_ui::UiValueSlider>(name_str);
    if (class_name == "UiVectorBox") return &target_parent.emplace_child<cd::elements_ui::UiVectorBox>(name_str);
    if (class_name == "UiGlyphText") return &target_parent.emplace_child<cd::elements_ui::UiGlyphText>(name_str);
    if (class_name == "UiGaugeBar") return &target_parent.emplace_child<cd::elements_ui::UiGaugeBar>(name_str);

    if (class_name == "Vessel") return &target_parent.emplace_child<cd::gameplay_vessel::Vessel>(name_str);
    if (class_name == "Avatar") return &target_parent.emplace_child<cd::gameplay_vessel::Avatar>(name_str);
    if (class_name == "RoverVessel") return &target_parent.emplace_child<cd::gameplay_vessel::RoverVessel>(name_str);
    if (class_name == "HumanPilot") return &target_parent.emplace_child<cd::gameplay_pilot::HumanPilot>(name_str);
    if (class_name == "SynthPilot") return &target_parent.emplace_child<cd::gameplay_pilot::SynthPilot>(name_str);
    if (class_name == "StageDirector") return &target_parent.emplace_child<cd::gameplay_director::StageDirector>(name_str);
    if (class_name == "WorldLedger") return &target_parent.emplace_child<cd::gameplay_director::WorldLedger>(name_str);
    if (class_name == "SpawnBeacon3D") return &target_parent.emplace_child<cd::gameplay_director::SpawnBeacon3D>(name_str);

    if (class_name == "Planar2D") return &target_parent.emplace_child<cd::element_core::Planar2D>(name_str);
    if (class_name == "Spatial3D") return &target_parent.emplace_child<cd::element_core::Spatial3D>(name_str);

    return &target_parent.emplace_child<cd::element_core::Element>(name_str);
}

namespace
{

class SpawnElementCommand final : public cd::editor::ICommand
{
public:
    SpawnElementCommand(
        const AddElementCatalog& catalog,
        cd::element_core::Stage& stage,
        std::string class_name,
        std::string instance_name
    )
        : catalog_(catalog)
        , stage_(stage)
        , class_name_(std::move(class_name))
        , instance_name_(std::move(instance_name))
    {
    }

    void apply() override
    {
        if (detached_ != nullptr)
        {
            spawned_ = stage_.root().attach_child(std::move(detached_));
        }
        else
        {
            spawned_ = catalog_.spawn_into_stage(stage_, class_name_, instance_name_);
        }
    }

    void revert() override
    {
        if (spawned_ != nullptr)
        {
            detached_ = stage_.root().detach_child(spawned_);
            spawned_ = nullptr;
        }
    }

    [[nodiscard]] std::string_view label() const noexcept override { return "SpawnElement"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }
    [[nodiscard]] cd::element_core::Element* spawned() const noexcept { return spawned_; }

private:
    const AddElementCatalog& catalog_;
    cd::element_core::Stage& stage_;
    std::string class_name_;
    std::string instance_name_;
    cd::element_core::Element* spawned_ { nullptr };
    std::unique_ptr<cd::element_core::Element> detached_ {};
};

class SpatialGizmoEditCommand final : public cd::editor::ICommand
{
public:
    SpatialGizmoEditCommand(
        cd::element_core::Spatial3D& target,
        ViewportGizmoMode mode,
        std::array<float, 3> before,
        std::array<float, 3> after
    ) noexcept
        : target_(target)
        , mode_(mode)
        , before_(before)
        , after_(after)
    {
    }

    void apply() override { set_val(after_); }
    void revert() override { set_val(before_); }
    [[nodiscard]] std::string_view label() const noexcept override { return "GizmoTransform"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    void set_val(std::array<float, 3> v) noexcept
    {
        switch (mode_)
        {
        case ViewportGizmoMode::kTranslate:
            target_.set_local_position(v[0], v[1], v[2]);
            break;
        case ViewportGizmoMode::kRotate:
            target_.set_local_euler_deg(v[0], v[1], v[2]);
            break;
        case ViewportGizmoMode::kScale:
            target_.set_local_scale(v[0], v[1], v[2]);
            break;
        }
    }

    cd::element_core::Spatial3D& target_;
    ViewportGizmoMode mode_;
    std::array<float, 3> before_;
    std::array<float, 3> after_;
};

}  // namespace

cd::element_core::Element* AddElementCatalog::spawn_with_undo(
    cd::element_core::Stage& stage,
    cd::editor::EditHistory& history,
    std::string_view class_name,
    std::string_view instance_name
) const
{
    auto cmd = std::make_unique<SpawnElementCommand>(
        *this,
        stage,
        std::string { class_name },
        std::string { instance_name }
    );
    auto* raw_cmd = cmd.get();
    history.push(std::move(cmd));
    return raw_cmd->spawned();
}

std::size_t ElementInspectorBinder::bind_element_to_grid(
    const cd::element_core::Element& element,
    cd::ui::widgets::Table& grid
)
{
    std::vector<cd::element_core::PropertyDesc> props;
    element.reflect_properties(props);
    static const std::array<cd::ui::widgets::Column, 3> kCols {{
        { "Property", 120.0F, true },
        { "Category", 90.0F, true },
        { "Value", 120.0F, false },
    }};
    grid.set_columns(kCols);
    grid.set_row_count(props.size());
    for (std::size_t i = 0; i < props.size(); ++i)
    {
        grid.set_cell_text(i, 0U, props[i].name);
        grid.set_cell_text(i, 1U, props[i].category);
        grid.set_cell_text(i, 2U, props[i].serialized_value);
    }
    return props.size();
}

bool ElementInspectorBinder::apply_spatial_edit(
    cd::element_core::Element& element,
    std::string_view property_name,
    std::array<float, 3> vec3_value
) noexcept
{
    auto* spatial = dynamic_cast<cd::element_core::Spatial3D*>(&element);
    if (spatial == nullptr)
        return false;

    if (property_name == "local_position")
    {
        spatial->set_local_position(vec3_value[0], vec3_value[1], vec3_value[2]);
        return true;
    }
    if (property_name == "local_euler_deg")
    {
        spatial->set_local_euler_deg(vec3_value[0], vec3_value[1], vec3_value[2]);
        return true;
    }
    if (property_name == "local_scale")
    {
        spatial->set_local_scale(vec3_value[0], vec3_value[1], vec3_value[2]);
        return true;
    }
    return false;
}

float ViewportGizmoController::snap_scalar(float value, float step) noexcept
{
    if (step <= 1e-5F)
        return value;
    return std::round(value / step) * step;
}

void ViewportGizmoController::apply_gizmo_manipulation(
    cd::element_core::Spatial3D& target,
    cd::editor::EditHistory& history,
    std::array<float, 3> raw_delta
) const
{
    std::array<float, 3> before {};
    float step = 0.0F;
    switch (mode)
    {
    case ViewportGizmoMode::kTranslate:
        before = target.local_position();
        step = snap.enable_snap ? snap.translate_step_m : 0.0F;
        break;
    case ViewportGizmoMode::kRotate:
        before = target.local_euler_deg();
        step = snap.enable_snap ? snap.rotate_step_deg : 0.0F;
        break;
    case ViewportGizmoMode::kScale:
        before = target.local_scale();
        step = snap.enable_snap ? snap.scale_step : 0.0F;
        break;
    }

    std::array<float, 3> after {
        snap_scalar(before[0] + raw_delta[0], step),
        snap_scalar(before[1] + raw_delta[1], step),
        snap_scalar(before[2] + raw_delta[2], step),
    };

    history.push(std::make_unique<SpatialGizmoEditCommand>(target, mode, before, after));
}

cd::element_core::Spatial3D* ViewportGizmoController::pick_spatial_element(
    cd::element_core::Stage& stage,
    std::array<float, 3> ray_origin,
    std::array<float, 3> ray_dir_normalized,
    float pick_radius_m
) noexcept
{
    const auto candidates = stage.find_all<cd::element_core::Spatial3D>();
    cd::element_core::Spatial3D* best = nullptr;
    float best_t = std::numeric_limits<float>::max();

    for (auto* s : candidates)
    {
        if (s == &stage.root())
            continue;
        const auto center = s->world_position();
        const float ox = ray_origin[0] - center[0];
        const float oy = ray_origin[1] - center[1];
        const float oz = ray_origin[2] - center[2];

        const float b = ox * ray_dir_normalized[0] + oy * ray_dir_normalized[1] + oz * ray_dir_normalized[2];
        const float c = (ox * ox + oy * oy + oz * oz) - (pick_radius_m * pick_radius_m);
        const float disc = b * b - c;
        if (disc < 0.0F)
            continue;
        const float t = -b - std::sqrt(disc);
        const float hit_t = (t >= 0.0F) ? t : (-b + std::sqrt(disc));
        if (hit_t >= 0.0F && hit_t < best_t)
        {
            best_t = hit_t;
            best = s;
        }
    }
    return best;
}

std::size_t TerrainFoliageBrush::apply_heightfield_stroke(
    std::vector<float>& heights,
    std::uint32_t width,
    std::uint32_t height,
    float center_x,
    float center_z
) const noexcept
{
    if (width == 0 || height == 0 || heights.size() < static_cast<std::size_t>(width) * height)
        return 0;

    std::size_t modified = 0;
    const float r2 = brush_radius_cells * brush_radius_cells;
    for (std::uint32_t z = 0; z < height; ++z)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float dx = static_cast<float>(x) - center_x;
            const float dz = static_cast<float>(z) - center_z;
            const float d2 = dx * dx + dz * dz;
            if (d2 > r2)
                continue;

            const float falloff = 1.0F - std::sqrt(d2) / std::max(brush_radius_cells, 0.001F);
            float& h = heights[static_cast<std::size_t>(z) * width + x];
            switch (op)
            {
            case SculptOp::kRaise:
                h += brush_strength * falloff;
                break;
            case SculptOp::kLower:
                h -= brush_strength * falloff;
                break;
            case SculptOp::kFlatten:
                h = h + (flatten_target_height - h) * std::clamp(brush_strength * falloff, 0.0F, 1.0F);
                break;
            case SculptOp::kSmooth:
                h *= (1.0F - 0.25F * falloff);
                break;
            }
            ++modified;
        }
    }
    return modified;
}

std::uint32_t TerrainFoliageBrush::paint_foliage_cluster(
    cd::elements_visual::SwarmMesh3D& swarm,
    std::uint32_t add_instances,
    float density_delta
) noexcept
{
    swarm.instance_count += add_instances;
    swarm.cull_distance_meters = std::max(10.0F, swarm.cull_distance_meters + density_delta * 10.0F);
    return swarm.instance_count;
}

// =============================================================================
// Wave 7.3 — PieWorkbenchController, VisualGraphWorkbench, GpuFrameProfilerExporter
// =============================================================================

bool PieWorkbenchController::play(cd::element_core::Stage& editor_stage)
{
    if (state_ != PieState::kStopped)
        return false;
    saved_spatials_.clear();
    for (auto* s : editor_stage.find_all<cd::element_core::Spatial3D>())
    {
        saved_spatials_.push_back(SavedSpatialState {
            s->uuid(),
            s->local_position(),
            s->local_euler_deg(),
            s->local_scale()
        });
    }
    loop_.begin_pie_session();
    state_ = PieState::kPlaying;
    pie_frames_ = 0;
    return true;
}

bool PieWorkbenchController::pause() noexcept
{
    if (state_ != PieState::kPlaying)
        return false;
    loop_.pause_pie_session(true);
    state_ = PieState::kPaused;
    return true;
}

bool PieWorkbenchController::resume() noexcept
{
    if (state_ != PieState::kPaused)
        return false;
    loop_.pause_pie_session(false);
    state_ = PieState::kPlaying;
    return true;
}

bool PieWorkbenchController::step_one_frame(cd::element_core::Stage& editor_stage, float dt)
{
    if (state_ == PieState::kStopped)
        return false;
    editor_stage.fixed_tick(dt);
    editor_stage.tick(dt);
    (void)loop_.step_frame(dt);
    ++pie_frames_;
    return true;
}

bool PieWorkbenchController::stop(cd::element_core::Stage& editor_stage)
{
    if (state_ == PieState::kStopped)
        return false;
    for (const auto& saved : saved_spatials_)
    {
        if (auto* elem = editor_stage.find_by_uuid(saved.uuid))
        {
            if (auto* s = dynamic_cast<cd::element_core::Spatial3D*>(elem))
            {
                s->set_local_position(saved.pos[0], saved.pos[1], saved.pos[2]);
                s->set_local_euler_deg(saved.euler[0], saved.euler[1], saved.euler[2]);
                s->set_local_scale(saved.scale[0], saved.scale[1], saved.scale[2]);
            }
        }
    }
    saved_spatials_.clear();
    loop_.stop_pie_session();
    state_ = PieState::kStopped;
    return true;
}

bool PieWorkbenchController::tick_if_playing(cd::element_core::Stage& editor_stage, float dt)
{
    if (state_ != PieState::kPlaying)
        return false;
    editor_stage.fixed_tick(dt);
    editor_stage.tick(dt);
    (void)loop_.step_frame(dt);
    ++pie_frames_;
    return true;
}

void VisualGraphWorkbench::populate_domain_graph(
    GraphDomain domain,
    cd::ui::widgets::NodeGraphCanvas& canvas
)
{
    switch (domain)
    {
    case GraphDomain::kBehaviorTree:
        (void)canvas.add_node({ 1U, "RootSelector", 40.0F, 60.0F, 140.0F, 64.0F, 1U, 2U });
        (void)canvas.add_node({ 2U, "CheckLineOfSight", 240.0F, 20.0F, 150.0F, 64.0F, 1U, 1U });
        (void)canvas.add_node({ 3U, "MoveToCover", 240.0F, 110.0F, 150.0F, 64.0F, 1U, 1U });
        (void)canvas.add_wire({ 1U, 0U, 2U, 0U });
        (void)canvas.add_wire({ 1U, 1U, 3U, 0U });
        break;
    case GraphDomain::kDialogueTree:
        (void)canvas.add_node({ 10U, "CommanderGreeting", 30.0F, 50.0F, 160.0F, 64.0F, 1U, 2U });
        (void)canvas.add_node({ 11U, "AcceptMissionBranch", 250.0F, 20.0F, 160.0F, 64.0F, 1U, 1U });
        (void)canvas.add_node({ 12U, "AskForIntelBranch", 250.0F, 100.0F, 160.0F, 64.0F, 1U, 1U });
        (void)canvas.add_wire({ 10U, 0U, 11U, 0U });
        (void)canvas.add_wire({ 10U, 1U, 12U, 0U });
        break;
    case GraphDomain::kAnimStateAndIk:
        (void)canvas.add_node({ 20U, "IdleWalkRunBlendSpace", 30.0F, 50.0F, 170.0F, 64.0F, 1U, 1U });
        (void)canvas.add_node({ 21U, "TwoBoneFootIkSolver", 260.0F, 50.0F, 170.0F, 64.0F, 1U, 1U });
        (void)canvas.add_wire({ 20U, 0U, 21U, 0U });
        break;
    case GraphDomain::kMaterialShaderGraph:
        (void)canvas.add_node({ 30U, "AlbedoRoughnessSampler", 30.0F, 40.0F, 160.0F, 64.0F, 1U, 1U });
        (void)canvas.add_node({ 31U, "ClearcoatSheenBsdf", 240.0F, 40.0F, 160.0F, 64.0F, 1U, 1U });
        (void)canvas.add_node({ 32U, "SurfaceOutputPbr", 450.0F, 40.0F, 150.0F, 64.0F, 1U, 1U });
        (void)canvas.add_wire({ 30U, 0U, 31U, 0U });
        (void)canvas.add_wire({ 31U, 0U, 32U, 0U });
        break;
    }
}

void GpuFrameProfilerExporter::record_pass(
    std::string pass_name,
    std::string category,
    std::uint64_t start_us,
    std::uint64_t duration_us
)
{
    samples_.push_back(GpuPassTimestampSample {
        std::move(pass_name),
        std::move(category),
        start_us,
        duration_us
    });
}

std::string GpuFrameProfilerExporter::export_chrome_tracing_json() const
{
    std::ostringstream out;
    out << "{\"traceEvents\":[";
    for (std::size_t i = 0; i < samples_.size(); ++i)
    {
        if (i > 0)
            out << ",";
        const auto& s = samples_[i];
        out << "{\"name\":\"" << s.pass_name
            << "\",\"cat\":\"" << s.queue_category
            << "\",\"ph\":\"X\",\"ts\":" << s.start_us
            << ",\"dur\":" << s.duration_us
            << ",\"pid\":1,\"tid\":1}";
    }
    out << "]}\n";
    return out.str();
}

}  // namespace cd::editor
