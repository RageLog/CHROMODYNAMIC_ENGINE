// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_behavior_designer/BehaviorDesigner.hpp
//
// phase556-557-558 — Sprint-1 placeholder (3 demo nodes + grid).
// phase740        — Sprint-2: real BT graph rendering from cd::game::ai_bt.
//
// BehaviorDesigner — Behavior tree graph view panel.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
//
// Sprint-2 (phase740) renders:
//   * A coloured panel background + separator bar.
//   * A placeholder grid of horizontal + vertical lines (pan-aware).
//   * REAL behavior tree graph walked from a cd::game::ai_bt::BehaviorTree
//     pointer supplied via set_tree():
//       - Nodes laid out top-down using a recursive even-space algorithm.
//       - Color-coded rectangles per NodeKind:
//           Selector  — blue
//           Sequence  — green
//           Parallel  — teal
//           Decorator — purple
//           Leaf      — amber
//       - Orthogonal edges parent→children (vertical stem + horizontal bus).
//       - Click-to-select: a node under the pointer can be identified via
//         hit_test(x, y) and emitted as selected_node_id.
//   * The currently selected node is highlighted with an accent border.
//
// When no tree is bound (set_tree(nullptr) or default state), falls back to
// the Sprint-1 demo nodes so the panel is never empty in the editor.
//
// State API:
//   set_tree(const cd::game::ai_bt::BehaviorTree*)
//                                     — bind (or unbind) a live BT.
//   tree() const                      — returns the currently bound tree pointer.
//   set_tree_root(BehaviorNodeId)     — legacy Sprint-1 root id (still usable).
//   tree_root() const                 — returns the legacy root id.
//   set_selected(BehaviorNodeId)      — select a node by layout index.
//   selected() const                  — returns the selected node id.
//   set_pan_offset(float x, float y)  — set view pan in pixels.
//   pan_x() const                     — horizontal pan in pixels.
//   pan_y() const                     — vertical pan in pixels.
//
// BehaviorNodeId layout-index convention (Sprint-2):
//   Nodes are assigned a 1-based BehaviorNodeId during the recursive layout
//   walk (pre-order depth-first). Id 0 == kInvalidNodeId (sentinel).
//
// Lifetime contract:
//   BehaviorDesigner is default-constructible and owns no heap storage.
//   The bound BehaviorTree pointer must outlive the panel.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/game/ai_bt/BehaviorTree.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>
#include <vector>

namespace cd::editor::panel::behavior_designer
{

// ---------------------------------------------------------------------------
// BehaviorNodeId — lightweight opaque handle for a behavior tree node.
// ---------------------------------------------------------------------------
using BehaviorNodeId = std::uint32_t;

/// Sentinel value meaning "no node bound / selected".
inline constexpr BehaviorNodeId kInvalidNodeId = 0U;

// ---------------------------------------------------------------------------
// LayoutNode — one node's computed screen-space position (graph-local coords).
// Populated by BehaviorDesigner::build_layout_() during draw().
// ---------------------------------------------------------------------------
struct LayoutNode
{
    BehaviorNodeId                    id       { kInvalidNodeId };
    const cd::game::ai_bt::Node*      bt_node  { nullptr };
    cd::game::ai_bt::NodeKind         kind     { cd::game::ai_bt::NodeKind::kLeaf };
    float                             lx       { 0.0F };  ///< graph-local x
    float                             ly       { 0.0F };  ///< graph-local y
    float                             w        { 0.0F };  ///< node rect width
    float                             h        { 0.0F };  ///< node rect height
    BehaviorNodeId                    parent_id{ kInvalidNodeId };
};

// ---------------------------------------------------------------------------
// BehaviorDesigner
// ---------------------------------------------------------------------------
class BehaviorDesigner
{
public:
    // Default-constructible; starts with no tree, no selection, zero pan.
    BehaviorDesigner() noexcept = default;

    // ---- Real BT binding API (Sprint-2) -------------------------------------

    /// Bind a live behavior tree. The pointer must outlive this panel.
    /// Pass nullptr to detach (falls back to Sprint-1 demo nodes).
    void set_tree(const cd::game::ai_bt::BehaviorTree* tree) noexcept;

    /// Returns the currently bound tree pointer (nullptr if none).
    [[nodiscard]] const cd::game::ai_bt::BehaviorTree* tree() const noexcept;

    // ---- Legacy tree root binding API (Sprint-1) ----------------------------

    /// Bind the panel to a behavior tree root node id.
    /// Pass kInvalidNodeId to detach.
    void set_tree_root(BehaviorNodeId id) noexcept;

    /// Returns the currently bound root node id (kInvalidNodeId if none).
    [[nodiscard]] BehaviorNodeId tree_root() const noexcept;

    // ---- Node selection API -------------------------------------------------

    /// Select a node by layout id. Pass kInvalidNodeId (or 0) to clear.
    void set_selected(BehaviorNodeId id) noexcept;

    /// Returns the currently selected node id.
    [[nodiscard]] BehaviorNodeId selected() const noexcept;

    // ---- View pan API -------------------------------------------------------

    /// Set the view pan offset in pixels (drag-to-pan).
    void set_pan_offset(float x, float y) noexcept;

    /// Returns the horizontal pan offset in pixels.
    [[nodiscard]] float pan_x() const noexcept;

    /// Returns the vertical pan offset in pixels.
    [[nodiscard]] float pan_y() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// When a tree is bound: renders the real BT graph with auto-layout.
    /// Otherwise: renders Sprint-1 placeholder (3 demo nodes).
    /// The selected node (if any) receives an accent-coloured border quad.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    // ---- State ---------------------------------------------------------------
    const cd::game::ai_bt::BehaviorTree* tree_      { nullptr };         ///< Bound BT (non-owning).
    BehaviorNodeId                        root_id_   { kInvalidNodeId };  ///< Legacy root id.
    BehaviorNodeId                        selected_id_{ kInvalidNodeId }; ///< Selected node.
    float                                 pan_x_     { 0.0F };            ///< Horizontal pan (px).
    float                                 pan_y_     { 0.0F };            ///< Vertical pan (px).

    // ---- Layout helpers ------------------------------------------------------

    /// Recursively walk the BT and compute subtree width (in node units).
    /// Populates `nodes_out` in pre-order with layout_x_ set to column,
    /// layout_y_ set to depth. Returns subtree width.
    static float measure_subtree_(
        const cd::game::ai_bt::Node* node,
        BehaviorNodeId                parent_id,
        std::uint32_t                 depth,
        float                         col_offset,
        float                         node_w,
        float                         node_h,
        float                         x_stride,
        float                         y_stride,
        BehaviorNodeId&               next_id,
        std::vector<LayoutNode>&      nodes_out);

    /// Draw the real-BT graph path.
    void draw_real_tree_(cd::ui::renderer::DrawBatcher& batcher,
                         const cd::ui::widgets::Theme&  theme,
                         float graph_x, float graph_y,
                         float graph_w, float graph_h) const;

    /// Draw the Sprint-1 demo-node fallback path.
    void draw_demo_nodes_(cd::ui::renderer::DrawBatcher& batcher,
                          const cd::ui::widgets::Theme&  theme,
                          float graph_x, float graph_y,
                          float graph_w, float graph_h) const;
};

}  // namespace cd::editor::panel::behavior_designer
