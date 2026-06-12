// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_dialog_tree_editor/DialogTreeEditor.hpp
//
// phase666 — cd::editor::panel::dialog_tree_editor  (panel_dialog_tree_editor library)
//
// Editor panel that renders a cd::game::dialog_tree::DialogTree as a
// visual graph. Provides a DrawBatcher-based draw path for the DockSpace
// shell (apps/editor).
//
// Sprint-1 renders:
//
//   * A coloured panel background.
//   * A separator bar under the title area (accent colour).
//   * Each DialogNode as a coloured filled rectangle, colour-coded by NodeKind:
//       - kSay       → blue   (NPC speech)
//       - kChoice    → yellow (player decision)
//       - kCondition → orange (invisible runtime gate)
//       - kEnd       → grey   (conversation terminal)
//   * One thin-quad edge per entry in each node's next_ids, drawn parent →
//     child as an L-shaped connector (vertical stem + horizontal bar + drop)
//     before nodes so nodes render on top.
//   * A per-depth column layout: all nodes at the same tree depth share a
//     horizontal band, advancing left-to-right within each depth column.
//     The layout is static for Sprint-1; pan/zoom is Sprint-2.
//   * The currently selected node (if any) gets an accent-coloured border
//     quad drawn behind its body.
//
// State API:
//   set_tree(const DialogTree*)    — bind to a dialog tree (null = detach).
//   selected_node_id() const       — returns the currently selected node id.
//   simulate_click(x, y, bounds)   — hit-test x/y against the last computed
//                                    node rects; selects the hit node.
//
// Lifetime contract:
//   DialogTreeEditor is default-constructible; it holds a non-owning raw
//   pointer to the DialogTree. The caller is responsible for ensuring that
//   the DialogTree outlives the panel. Calling set_tree(nullptr) detaches.
//
// MOMENT: A narrative designer opens the panel, sees the 200-node BG3-style
// dialog graph visually laid out, clicks any node to inspect its text —
// no JSON editor required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/game/dialog_tree/DialogTree.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <optional>
#include <string>
#include <vector>

namespace cd::editor::panel::dialog_tree_editor
{

// ---------------------------------------------------------------------------
// DialogTreeEditor
// ---------------------------------------------------------------------------
class DialogTreeEditor
{
public:
    // Default-constructible; starts with no tree and no selection.
    DialogTreeEditor() noexcept = default;

    // ---- Tree binding API ---------------------------------------------------

    /// Bind to a DialogTree. The panel holds a non-owning pointer; pass
    /// nullptr to detach. Clears the current selection and recomputes the
    /// node layout cache.
    void set_tree(const cd::game::dialog_tree::DialogTree* tree);

    // ---- Selection API -------------------------------------------------------

    /// Returns the selected node_id, or std::nullopt if nothing is selected.
    [[nodiscard]] std::optional<std::string> selected_node_id() const;

    /// Hit-test the point (x, y) in panel-local coordinates against the last
    /// computed node rects. Selects the hit node; clears selection if no node
    /// is hit. `bounds` must match the bounds passed to the most recent draw()
    /// call for the hit-test to be accurate.
    void simulate_click(float x, float y, const cd::ui::widgets::Rect& bounds);

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders: background, separator, per-node colour rectangles, edge
    /// connectors between parent and child nodes, and an accent border on the
    /// selected node.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              const cd::ui::widgets::Theme&    theme,
              const cd::ui::widgets::Rect&     bounds) const;

private:
    // ---- Internal layout cache ---------------------------------------------

    /// Per-node layout record computed during draw() and stored for
    /// simulate_click() hit-testing.
    struct NodeRect
    {
        std::string node_id {};
        float       x { 0.0F };
        float       y { 0.0F };
        float       w { 0.0F };
        float       h { 0.0F };

        [[nodiscard]] bool contains(float px, float py) const noexcept
        {
            return px >= x && py >= y && px < x + w && py < y + h;
        }
    };

    // ---- Helpers ------------------------------------------------------------

    /// Compute the depth of each node via BFS from the root. Nodes absent
    /// from the traversal (disconnected sub-graphs) are placed at depth 0.
    /// Returns a vector parallel to tree_->nodes in tree insertion order.
    void recompute_layout() const;

    // ---- State --------------------------------------------------------------
    const cd::game::dialog_tree::DialogTree* tree_        { nullptr };
    std::string                              selected_id_  {};        ///< empty = no selection
    mutable std::vector<NodeRect>            node_rects_  {};        ///< updated each draw()
};

}  // namespace cd::editor::panel::dialog_tree_editor
