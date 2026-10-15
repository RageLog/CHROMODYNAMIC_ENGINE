// =============================================================================
// CHROMODYNAMIC — cd/editor/FullFledgedEditorSuite.hpp
// FAZ 7 (Waves 7.1, 7.2, 7.3) — 100% cd::ui Full-Fledged Editor & Play-In-Editor:
//   * Wave 7.1: EditorCockpitShell — 27-panel 100% cd::ui::* shell (Dear ImGui-free)
//               + ProjectHub showcase/project browser.
//   * Wave 7.2: AddElementCatalog ("+ Add Element" for all 44+ Element/Vessel/Pilot
//               classes with EditHistory Undo/Redo), ElementInspectorBinder,
//               ViewportGizmoController (3D/2D Translate/Rotate/Scale + Snapping
//               + Ray Picking), TerrainFoliageBrush (Raise/Lower/Smooth/Flatten
//               + Foliage Density).
//   * Wave 7.3: PieWorkbenchController (Play/Pause/Step/Stop driving EngineLoop
//               with .cdstage snapshot & restore), VisualGraphWorkbench
//               (BehaviorDesigner / DialogTreeEditor / Animator+IK / MaterialEditor
//               node graph integration), GpuFrameProfilerExporter (Chrome Tracing JSON).
// =============================================================================
#pragma once

#include <cd/editor/EditHistory.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/elements_domain/DomainElements.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/runtime/EngineLoop.hpp>
#include <cd/ui/widgets/EditorAndHudWidgets.hpp>
#include <cd/ui/widgets/Table.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor
{

// =============================================================================
// Wave 7.1 — 100% cd::ui EditorCockpitShell & ProjectHub
// =============================================================================

struct ShowcaseProjectEntry
{
    cd::gameplay_director::ShowcaseId id { cd::gameplay_director::ShowcaseId::kPbrMaterialsSssHair };
    std::string slug {};
    std::string cdproj_relative_path {};
    std::string cdstage_relative_path {};
};

class EditorCockpitShell
{
public:
    static constexpr std::size_t kCanonicalPanelCount = 27U;

    EditorCockpitShell();

    [[nodiscard]] const std::vector<std::string>& panel_ids() const noexcept { return panel_ids_; }
    [[nodiscard]] std::size_t registered_panel_count() const noexcept { return panel_ids_.size(); }
    [[nodiscard]] bool uses_dear_imgui() const noexcept { return false; }

    [[nodiscard]] cd::ui::widgets::Splitter& main_splitter() noexcept { return main_splitter_; }
    [[nodiscard]] const cd::ui::widgets::Splitter& main_splitter() const noexcept { return main_splitter_; }

    [[nodiscard]] const std::vector<ShowcaseProjectEntry>& project_hub_showcases() const noexcept
    {
        return showcases_;
    }

    /// Open one of the 12 built-in Showcase projects into a live `Stage`.
    [[nodiscard]] cd::element_core::Stage open_showcase_in_editor(
        cd::gameplay_director::ShowcaseId id
    ) const;

private:
    std::vector<std::string> panel_ids_ {};
    cd::ui::widgets::Splitter main_splitter_ { true, 0.22F };
    std::vector<ShowcaseProjectEntry> showcases_ {};
};

// =============================================================================
// Wave 7.2 — "+ Add Element" Catalog, Dynamic Inspector, Gizmo & Terrain Brush
// =============================================================================

struct ElementCatalogItem
{
    std::string class_name {};
    std::string category {};
    std::string description {};
};

class AddElementCatalog
{
public:
    AddElementCatalog();

    [[nodiscard]] const std::vector<ElementCatalogItem>& items() const noexcept { return items_; }
    [[nodiscard]] std::size_t count() const noexcept { return items_.size(); }

    [[nodiscard]] std::vector<ElementCatalogItem> filter_by_category(std::string_view category) const;
    [[nodiscard]] std::vector<ElementCatalogItem> search(std::string_view query) const;

    /// Spawn any registered CHROMODYNAMIC `Element` / `Vessel` / `Pilot` / `StageDirector`
    /// subclass into `stage` (under `parent` or `stage.root()`) by its `class_name`.
    cd::element_core::Element* spawn_into_stage(
        cd::element_core::Stage& stage,
        std::string_view class_name,
        std::string_view instance_name = {},
        cd::element_core::Element* parent = nullptr
    ) const;

    /// Spawn an element and push an undoable `ICommand` into `history`.
    cd::element_core::Element* spawn_with_undo(
        cd::element_core::Stage& stage,
        cd::editor::EditHistory& history,
        std::string_view class_name,
        std::string_view instance_name = {}
    ) const;

private:
    std::vector<ElementCatalogItem> items_ {};
};

class ElementInspectorBinder
{
public:
    /// Populate a `cd::ui::widgets::Table` property grid from `element.reflect_properties()`.
    static std::size_t bind_element_to_grid(
        const cd::element_core::Element& element,
        cd::ui::widgets::Table& grid
    );

    /// Apply a live edit from the Inspector to a `Spatial3D` or `Element`.
    static bool apply_spatial_edit(
        cd::element_core::Element& element,
        std::string_view property_name,
        std::array<float, 3> vec3_value
    ) noexcept;
};

enum class ViewportGizmoMode : std::uint8_t
{
    kTranslate = 0,
    kRotate = 1,
    kScale = 2,
};

struct GizmoSnapSettings
{
    bool  enable_snap { true };
    float translate_step_m { 0.5F };
    float rotate_step_deg { 15.0F };
    float scale_step { 0.25F };
};

class ViewportGizmoController
{
public:
    ViewportGizmoMode mode { ViewportGizmoMode::kTranslate };
    GizmoSnapSettings snap {};

    [[nodiscard]] static float snap_scalar(float value, float step) noexcept;

    /// Apply a gizmo drag delta to `target` with optional snapping and record an Undo command.
    void apply_gizmo_manipulation(
        cd::element_core::Spatial3D& target,
        cd::editor::EditHistory& history,
        std::array<float, 3> raw_delta
    ) const;

    /// Ray-sphere picking in viewport world space: returns the closest hit `Spatial3D*`.
    [[nodiscard]] static cd::element_core::Spatial3D* pick_spatial_element(
        cd::element_core::Stage& stage,
        std::array<float, 3> ray_origin,
        std::array<float, 3> ray_dir_normalized,
        float pick_radius_m = 1.5F
    ) noexcept;
};

class TerrainFoliageBrush
{
public:
    enum class SculptOp : std::uint8_t
    {
        kRaise = 0,
        kLower = 1,
        kSmooth = 2,
        kFlatten = 3,
    };

    SculptOp op { SculptOp::kRaise };
    float brush_radius_cells { 3.0F };
    float brush_strength { 1.0F };
    float flatten_target_height { 0.0F };

    /// Apply a brush stroke at `(center_x, center_z)` on a `width x height` heightfield.
    std::size_t apply_heightfield_stroke(
        std::vector<float>& heights,
        std::uint32_t width,
        std::uint32_t height,
        float center_x,
        float center_z
    ) const noexcept;

    /// Paint foliage instances onto a `SwarmMesh3D` element.
    static std::uint32_t paint_foliage_cluster(
        cd::elements_visual::SwarmMesh3D& swarm,
        std::uint32_t add_instances,
        float density_delta = 0.1F
    ) noexcept;
};

// =============================================================================
// Wave 7.3 — Play-In-Editor (PIE), Visual Graph Workbench & GPU Frame Profiler
// =============================================================================

enum class PieState : std::uint8_t
{
    kStopped = 0,
    kPlaying = 1,
    kPaused = 2,
};

class PieWorkbenchController
{
public:
    explicit PieWorkbenchController(cd::runtime::EngineLoop& loop) noexcept
        : loop_(loop)
    {
    }

    bool play(cd::element_core::Stage& editor_stage);
    bool pause() noexcept;
    bool resume() noexcept;
    bool step_one_frame(cd::element_core::Stage& editor_stage, float dt = 1.0F / 60.0F);
    bool stop(cd::element_core::Stage& editor_stage);

    bool tick_if_playing(cd::element_core::Stage& editor_stage, float dt);

    [[nodiscard]] PieState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t pie_frames_executed() const noexcept { return pie_frames_; }

private:
    struct SavedSpatialState
    {
        std::uint64_t uuid { 0 };
        std::array<float, 3> pos {};
        std::array<float, 3> euler {};
        std::array<float, 3> scale {};
    };

    cd::runtime::EngineLoop& loop_;
    PieState state_ { PieState::kStopped };
    std::uint64_t pie_frames_ { 0 };
    std::vector<SavedSpatialState> saved_spatials_ {};
};

class VisualGraphWorkbench
{
public:
    enum class GraphDomain : std::uint8_t
    {
        kBehaviorTree = 0,
        kDialogueTree = 1,
        kAnimStateAndIk = 2,
        kMaterialShaderGraph = 3,
    };

    /// Build a starter visual node graph on `canvas` for the given authoring domain.
    static void populate_domain_graph(
        GraphDomain domain,
        cd::ui::widgets::NodeGraphCanvas& canvas
    );
};

struct GpuPassTimestampSample
{
    std::string pass_name {};
    std::string queue_category { "GPU" };
    std::uint64_t start_us { 0 };
    std::uint64_t duration_us { 0 };
};

class GpuFrameProfilerExporter
{
public:
    void clear() noexcept { samples_.clear(); }
    void record_pass(std::string pass_name, std::string category, std::uint64_t start_us, std::uint64_t duration_us);
    [[nodiscard]] std::size_t sample_count() const noexcept { return samples_.size(); }

    /// Export recorded CPU + GPU pass spans as a Chrome Tracing (`chrome://tracing`) JSON document.
    [[nodiscard]] std::string export_chrome_tracing_json() const;

private:
    std::vector<GpuPassTimestampSample> samples_ {};
};

}  // namespace cd::editor
