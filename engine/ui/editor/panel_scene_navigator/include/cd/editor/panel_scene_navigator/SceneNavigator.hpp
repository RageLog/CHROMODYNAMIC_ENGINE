// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_scene_navigator/SceneNavigator.hpp
//
// phase687 — cd::editor::panel::scene_navigator  (panel_scene_navigator library)
//
// Scene Navigator panel: search box at the top + a live-filtered, scrollable
// list of ECS entities below. Sibling to scene_tree (which shows the parent/
// child hierarchy); this panel is the SEARCH/FILTER/BREADCRUMB concern.
//
// Designer types a substring into the search box and the entity list narrows
// to only matching names in real time. Escape clears the filter and shows all
// entities again.
//
// State API:
//   set_entities(span<Entity>, span<string> names)
//                             — load the flat entity list (parallel arrays).
//                               Copies both spans; caller may release them.
//   set_filter(string_view)   — replace the current filter text (normalised
//                               to lower-case internally for case-insensitive
//                               matching). Recomputes filtered_indices_.
//   selected_entity() const   — the currently selected entity (nullopt when
//                               nothing is selected or the entity list is empty).
//   filtered_indices() const  — span over the indices into the entity list
//                               that currently match the filter.
//   simulate_click(x, y, bounds)
//                             — hit-test against the filtered-row geometry;
//                               selects the row the click lands on.
//   simulate_text_input(typed)— appends `typed` to the current filter (models
//                               the user typing into the search box); calls
//                               set_filter() internally.
//
// Draw (DrawBatcher path — DockSpace / apps/editor):
//   draw(batcher, theme, bounds) const
//     — emits quads for:
//         1. Panel background fill.
//         2. Search box background (accent-tinted).
//         3. Accent separator bar below the search box.
//         4. One row per filtered entity; selected row is accent-highlighted.
//
// Lifetime contract:
//   SceneNavigator is default-constructible and owns no external resources.
//   All data is stored by value (std::vector / std::string). Thread-safe for
//   read; write API must be called from a single thread.
//
// MOMENT: A designer working on a 500-entity scene types 'enem' and instantly
// sees all 12 enemy spawners — no scrolling, no manual sort.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor::panel::scene_navigator
{

// ---------------------------------------------------------------------------
// SceneNavigator
// ---------------------------------------------------------------------------
class SceneNavigator
{
public:
    // Default-constructible; starts with empty entity list and no selection.
    SceneNavigator() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Load the entity list from two parallel arrays. Lengths must match;
    /// both spans are consumed immediately. Recomputes filtered_indices_.
    void set_entities(std::span<const cd::ecs::Entity> entities,
                      std::span<const std::string>     names);

    /// Replace the current filter substring (case-insensitive). An empty
    /// string clears the filter and shows all entities.
    void set_filter(std::string_view substring);

    /// Returns the currently selected entity, or std::nullopt when nothing
    /// is selected.
    [[nodiscard]] std::optional<cd::ecs::Entity> selected_entity() const noexcept;

    /// Returns a span over the indices (into entities_) that match the
    /// current filter. Empty filter → all indices.
    [[nodiscard]] std::span<const std::size_t> filtered_indices() const noexcept;

    // ---- Interaction helpers (testing + shell integration) ------------------

    /// Perform a hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the entity whose row the click lands on. No-op if outside panel
    /// or in the search-box area.
    void simulate_click(float x, float y, const cd::ui::widgets::Rect& bounds) noexcept;

    /// Append `typed` to the current filter text, then recompute the list.
    /// Models the user typing characters into the search box. Passing an
    /// empty view is a no-op.
    void simulate_text_input(std::string_view typed);

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background fill.
    ///   2. Search-box background quad (slightly lighter surface).
    ///   3. Accent separator bar below the search box.
    ///   4. Filtered entity rows; the selected row gets an accent highlight.
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Stored data --------------------------------------------------------
    std::vector<cd::ecs::Entity> entities_    {};  ///< Full entity list.
    std::vector<std::string>     names_       {};  ///< Display names (parallel to entities_).
    std::string                  filter_      {};  ///< Lower-cased filter substring.
    std::vector<std::size_t>     filtered_    {};  ///< Indices matching filter_.
    std::optional<cd::ecs::Entity> selected_  {};  ///< Currently selected entity.

    // ---- Internal helpers ---------------------------------------------------

    /// Recompute filtered_ from entities_/names_/filter_.
    void rebuild_filter() noexcept;

    // ---- Layout constants (shared between draw() and simulate_click()) ------
    static constexpr float kPad       =  6.0F;   ///< Outer horizontal/vertical padding.
    static constexpr float kSearchH   = 20.0F;   ///< Search box height.
    static constexpr float kBarH      =  4.0F;   ///< Accent separator bar height.
    static constexpr float kRowH      = 20.0F;   ///< Height of one entity row.
    static constexpr float kRowGap    =  2.0F;   ///< Gap between rows.

    /// Y offset (relative to bounds.y) where the first entity row begins.
    static constexpr float kListOffsetY =
        kPad + kSearchH + kPad + kBarH + kPad;

    /// Y coordinate of filtered row `i` relative to bounds.y.
    [[nodiscard]] static float row_top(std::size_t i) noexcept
    {
        return kListOffsetY + static_cast<float>(i) * (kRowH + kRowGap);
    }
};

}  // namespace cd::editor::panel::scene_navigator
