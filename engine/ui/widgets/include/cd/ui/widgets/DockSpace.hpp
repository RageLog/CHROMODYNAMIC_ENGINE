// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/DockSpace.hpp
//
// Phase 5.1.5 / T2.1 of the editor enablement track. ImGui dock-space
// semantic parity expressed as a pure-state widget the renderer / input
// pipeline can drive without a GPU.
//
// Conceptual model
// ----------------
// A DockSpace owns a tree of `DockNode` instances rooted at `root_`.
// Each node is exactly one of:
//
//   * kSplit     -- has two children + an `axis` (Vertical = side-by-side,
//                   Horizontal = top/bottom) + a `ratio` in (0..1) for the
//                   first child's share of the rect.
//   * kTabGroup  -- has 1..N panel ids drawn as tabs, with one `active_tab`
//                   index selecting which panel's body fills the rect
//                   below the tab strip.
//   * kLeaf      -- has 0 panels (placeholder used internally for empty
//                   slots after operations that detach the last panel).
//
// Public surface (T2.1 contract)
// ------------------------------
//   class DockNode { ... pure data ... }
//   class DockSpace
//     * register_panel(id, drawer)           -- map panel-id -> draw fn
//     * tick(InputState)                     -- run splitter drag + tab
//                                              click + drag-out / drag-in
//     * draw(DrawBatcher, Theme)             -- emit quads for splitters,
//                                              tab strips, drop-target hints
//                                              and tab BODIES (via the
//                                              registered drawer of the
//                                              active tab in each group).
//     * serialize() -> std::vector<std::byte>
//     * restore(span<const std::byte>) -> bool
//     * split(leaf_id, axis, new_panel_id, ratio)  -- programmatic split
//     * tab_merge(target_leaf_id, new_panel_id)    -- programmatic dock-into
//     * undock(panel_id)                            -- programmatic drag-out
//
// Drop targets
// ------------
// While the user holds the left button on a tab strip, the DockSpace
// enters a *drag-out* state. Releasing OVER a leaf surfaces one of five
// drop zones the cursor is currently inside:
//
//        top
//   left center right     <-- center = "merge as tab"
//        bottom
//
// Releasing on center merges the panel into the target's tab group.
// Releasing on top/bottom/left/right splits the target leaf along the
// appropriate axis and inserts the dragged panel into a new leaf on
// the chosen side. Releasing OUTSIDE any leaf detaches the panel into
// a floating leaf (root replaced by a split when needed) -- matching
// the "drag-out to undock" semantics from the brief.
//
// Serialisation format
// --------------------
// The serializer emits a compact little-endian byte stream. The first
// 8 bytes are the magic + version header `CDDOCK\0\1`. Then a recursive
// node descriptor:
//
//   byte  kind     (0 = split, 1 = tab, 2 = leaf)
//   if split:
//     byte  axis   (0 = vertical, 1 = horizontal)
//     f32   ratio
//     ...children...   (first, then second)
//   if tab:
//     u32   panel_count
//     u32   active_tab
//     for each panel:
//       u32 id_length
//       bytes id (utf-8, no terminator)
//   if leaf:
//     (no payload -- empty placeholder)
//
// `restore()` validates the magic + version and rebuilds the tree.
// Returns `false` on any framing / bound failure and leaves the
// dockspace unchanged (atomic restore guarantee).
//
// Why the brief notes "HARDEST single UI primitive"
// -------------------------------------------------
// DockSpace simultaneously is a layout solver (split rect math), an
// input router (splitter drag vs tab strip vs drop-target), a state
// machine (dragging vs idle), and a serializer. Each of the ten test
// cases below exercises ONE of those axes in isolation to keep the
// implementation regressable.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::ui::renderer
{
class DrawBatcher;
}
namespace cd::ui::font
{
class Font;
}

namespace cd::ui::widgets
{

// ---- Enums ----------------------------------------------------------------

/// Direction of a split. `kVertical` = children side-by-side (left/right);
/// `kHorizontal` = children stacked (top/bottom). Names match ImGui's
/// `ImGuiDir`-style: split axis named after the splitter handle's
/// orientation (a vertical splitter divides horizontally placed children).
enum class DockAxis : std::uint8_t
{
    kVertical   = 0,  ///< left / right children
    kHorizontal = 1,  ///< top  / bottom children
};

/// What kind of payload a DockNode carries.
enum class DockNodeKind : std::uint8_t
{
    kSplit    = 0,
    kTabGroup = 1,
    kLeaf     = 2,  ///< empty placeholder
};

/// One of the five drop zones surfaced when a drag-out hovers a leaf.
enum class DockDropZone : std::uint8_t
{
    kNone   = 0,
    kCenter = 1,  ///< merge as tab
    kTop    = 2,  ///< split kHorizontal, dragged panel on top
    kBottom = 3,  ///< split kHorizontal, dragged panel on bottom
    kLeft   = 4,  ///< split kVertical,   dragged panel on left
    kRight  = 5,  ///< split kVertical,   dragged panel on right
};

// ---- DockNode -------------------------------------------------------------

/// Pure-data tree node. The tree owns its children via `unique_ptr` so
/// destruction is automatic and recursive without explicit teardown.
class DockNode
{
public:
    DockNode() = default;
    explicit DockNode(DockNodeKind k) noexcept : kind_(k) {}

    DockNode(const DockNode&)            = delete;
    DockNode& operator=(const DockNode&) = delete;
    DockNode(DockNode&&)                 = default;
    DockNode& operator=(DockNode&&)      = default;

    [[nodiscard]] DockNodeKind kind() const noexcept { return kind_; }

    // -- split fields --
    [[nodiscard]] DockAxis axis()  const noexcept { return axis_; }
    [[nodiscard]] float    ratio() const noexcept { return ratio_; }
    void set_axis(DockAxis a) noexcept { axis_ = a; }
    /// Clamp ratio into [0.05, 0.95] to keep both children visible.
    void set_ratio(float r) noexcept;

    [[nodiscard]] DockNode*       first()        noexcept { return first_.get(); }
    [[nodiscard]] const DockNode* first()  const noexcept { return first_.get(); }
    [[nodiscard]] DockNode*       second()       noexcept { return second_.get(); }
    [[nodiscard]] const DockNode* second() const noexcept { return second_.get(); }

    // -- tab fields --
    [[nodiscard]] std::span<const std::string> panels() const noexcept
    {
        return std::span<const std::string>(panels_.data(), panels_.size());
    }
    [[nodiscard]] std::size_t active_tab() const noexcept { return active_tab_; }
    /// Set the active tab. Clamped to `panels().size() - 1` (no-op when empty).
    void set_active_tab(std::size_t idx) noexcept;

    // -- per-frame computed rect (filled by DockSpace::tick / draw walk) --
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }
    void set_rect(Rect r) noexcept { rect_ = r; }

    // -- friend builders used by DockSpace --
    friend class DockSpace;

    // -- friend factory helpers (defined below) --
    friend std::unique_ptr<DockNode> make_single_leaf(std::string panel_id);
    friend std::unique_ptr<DockNode> make_tab_group(std::vector<std::string> panel_ids);
    friend std::unique_ptr<DockNode> make_split(DockAxis axis,
                                                std::unique_ptr<DockNode> first,
                                                std::unique_ptr<DockNode> second,
                                                float ratio);

private:
    DockNodeKind kind_  { DockNodeKind::kLeaf };
    DockAxis     axis_  { DockAxis::kVertical };
    float        ratio_ { 0.5F };

    std::unique_ptr<DockNode> first_  {};
    std::unique_ptr<DockNode> second_ {};

    std::vector<std::string> panels_     {};
    std::size_t              active_tab_ { 0U };

    Rect rect_ {};  ///< last computed pixel rect (cache for hit-tests)
};

// ---- Splitter (divider drag) ---------------------------------------------

/// Splitter handle width / hover tolerance. The splitter is rendered as a
/// thin quad between the two children of a `kSplit` node. Hovering it
/// reports `state.hovered`; dragging it (`pressed` then `down`) updates
/// the parent split's `ratio` in real time.
struct Splitter
{
    static constexpr float kHandleThickness { 6.0F };  ///< visible bar
    static constexpr float kHitTolerance    { 8.0F };  ///< click target padding
};

// ---- TabStrip (per tab-group) --------------------------------------------

/// Tab strip layout / hit-test helpers exposed for tooling tests. The
/// DockSpace draws this for every kTabGroup node above the active panel
/// body. Tab order follows `node.panels()`.
struct TabStrip
{
    static constexpr float kHeight    { 22.0F };  ///< strip height in pixels
    static constexpr float kTabWidth  { 90.0F };  ///< nominal per-tab width
    static constexpr float kTabPad    { 4.0F };   ///< inset between tabs
};

// ---- DockSpace ------------------------------------------------------------

/// Top-level container. Owns the dock tree, the panel registry, and the
/// per-frame drag-out / drop-zone state machine.
class DockSpace
{
public:
    /// One panel's body drawer. Receives the panel's pixel rect and the
    /// shared draw batcher / theme so it can emit its own widgets inside
    /// the dock tile. Signature deliberately decoupled from the wider
    /// engine: dockspace tests pass a no-op lambda.
    using ContentDrawer = std::function<void(const Rect&,
                                             cd::ui::renderer::DrawBatcher&,
                                             cd::ui::font::Font*,
                                             const Theme&)>;

    DockSpace();

    DockSpace(const DockSpace&)            = delete;
    DockSpace& operator=(const DockSpace&) = delete;
    DockSpace(DockSpace&&)                 = default;
    DockSpace& operator=(DockSpace&&)      = default;

    /// Whole-space pixel rect. The root node inherits this every tick.
    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    /// The root of the dock tree. Replaced on `restore()` and on
    /// undock-into-floating-root operations.
    [[nodiscard]] DockNode*       root()       noexcept { return root_.get(); }
    [[nodiscard]] const DockNode* root() const noexcept { return root_.get(); }

    /// Register a panel id + its drawer. Calling twice with the same id
    /// overwrites the previous drawer. Returns true on first registration.
    bool register_panel(std::string id, ContentDrawer drawer);

    /// True when `id` was registered. Drawing skips unregistered panels.
    [[nodiscard]] bool has_panel(std::string_view id) const noexcept;

    /// Locate the leaf / tab-group that currently owns `panel_id`. Walks
    /// the tree; cost is O(N) in node count. Returns nullptr when not found.
    [[nodiscard]] DockNode*       find_panel_owner(std::string_view panel_id) noexcept;
    [[nodiscard]] const DockNode* find_panel_owner(std::string_view panel_id) const noexcept;

    /// Programmatic split. Replaces `target_leaf` with a new kSplit whose
    /// two children are the original (now wrapped) tab group on side
    /// `kFirst` and a freshly-built tab group containing `new_panel_id`
    /// on side `kSecond` -- where `kFirst/kSecond` depend on `axis`.
    /// Returns true on success. `ratio` is clamped.
    bool split(DockNode* target_leaf, DockAxis axis,
               std::string new_panel_id, float ratio = 0.5F);

    /// Append `new_panel_id` as a new tab in `target` (must be kTabGroup).
    /// Returns true on success; sets new panel as the active tab.
    bool tab_merge(DockNode* target, std::string new_panel_id);

    /// Detach `panel_id` from its current owner. If the owner ends up
    /// empty, it is collapsed (its sibling promoted into the parent).
    /// The detached panel becomes the sole tab of a new floating leaf
    /// stashed in `floating_panels_`; tests use `floating_count()` to
    /// observe this. Returns true on success.
    bool undock(std::string_view panel_id);

    /// Number of floating (undocked but not deleted) panels. Each is a
    /// single-tab DockNode owned by the dockspace.
    [[nodiscard]] std::size_t floating_count() const noexcept { return floating_panels_.size(); }

    /// Re-dock the front-most floating panel onto `target_leaf` using a
    /// given drop zone. Returns true on success. `kNone` is a no-op.
    bool drop_floating(DockNode* target_leaf, DockDropZone zone);

    /// Activate (focus) `panel_id` by selecting it as the active tab in
    /// its owning tab group. Returns true when the panel was found.
    bool activate_panel(std::string_view panel_id);

    /// Run pointer / key driven state transitions: splitter drag, tab
    /// click, drag-out, drop-target detection. Must be called once per
    /// frame before `draw`. `dt_s` is currently unused but reserved for
    /// future drag-spring animations.
    void tick(const InputState& input, float dt_s = 0.0F);

    /// Emit draw commands for splitters, tab strips, drop-target hint
    /// quads (when a drag is in flight), and the active-tab body of
    /// every tab group via its registered ContentDrawer.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

    /// Serialize the current tree into a portable byte stream.
    [[nodiscard]] std::vector<std::byte> serialize() const;

    /// Restore from a previously serialized stream. On any framing /
    /// bound failure returns false and leaves the dockspace unchanged.
    bool restore(std::span<const std::byte> bytes);

    // ---- Inspection helpers (used by tests / tooling) -------------------

    /// Currently in-flight drag-out panel id (empty when idle).
    [[nodiscard]] std::string_view dragging_panel() const noexcept { return dragging_panel_; }

    /// Drop zone the cursor would land on at the current pointer position
    /// (kNone when not dragging or not over a leaf).
    [[nodiscard]] DockDropZone current_drop_zone() const noexcept { return current_drop_zone_; }

    /// Leaf currently under the drag-out cursor (nullptr when idle).
    [[nodiscard]] const DockNode* drop_target() const noexcept { return current_drop_target_; }

    /// Count total nodes in the tree (split + tab + leaf). O(N).
    [[nodiscard]] std::size_t node_count() const noexcept;

private:
    Rect                                                rect_ {};
    std::unique_ptr<DockNode>                           root_;
    std::unordered_map<std::string, ContentDrawer>      panels_;

    // Drag-out state. `dragging_panel_` is the id being dragged; it is
    // detached from its origin owner ONLY on release (so cancel-drag
    // out-of-window scenarios leave the tree untouched).
    std::string                                         dragging_panel_;
    DockDropZone                                        current_drop_zone_   { DockDropZone::kNone };
    DockNode*                                           current_drop_target_ { nullptr };

    // Splitter drag state. Tracks which kSplit node is being resized this
    // frame so the next pointer-move applies the ratio update.
    DockNode*                                           dragging_splitter_ { nullptr };

    // Floating leaves. The front of this list is the "topmost" panel and
    // also the candidate for `drop_floating()`. Per the brief, dragging a
    // panel out and not dropping it on a leaf creates a floating leaf.
    std::vector<std::unique_ptr<DockNode>>              floating_panels_;

    // Pointer edge-tracking for drag-out latch: we need a click that
    // originated on a tab strip to remain a drag even when the cursor
    // moves outside the strip.
    bool                                                left_down_prev_ { false };

    // ---- internal helpers ----
    void compute_rects_(DockNode* node, const Rect& r) noexcept;
    [[nodiscard]] DockNode* hit_test_leaf_(DockNode* node, float px, float py) noexcept;
    [[nodiscard]] DockNode* hit_test_splitter_(DockNode* node, float px, float py) noexcept;
    [[nodiscard]] DockNode* hit_test_tab_strip_(DockNode* node, float px, float py,
                                                std::size_t& out_tab_idx,
                                                bool& out_inside_strip) noexcept;
    [[nodiscard]] static DockDropZone classify_drop_zone_(const Rect& leaf_rect,
                                                          float px, float py) noexcept;

    /// Find the parent of `target` in the tree rooted at `root_`. Returns
    /// nullptr when `target` is the root or not found.
    [[nodiscard]] DockNode* find_parent_(DockNode* target) noexcept;

    /// Collapse the parent of `child` by promoting `child`'s sibling into
    /// the parent's slot. No-op when parent is root or child has no sibling.
    void collapse_parent_of_(DockNode* child) noexcept;

    /// Remove a single panel id from a kTabGroup node. Returns true on
    /// success. If the panel was the active tab, the active index is
    /// clamped. If the node ends up empty, the caller should collapse.
    [[nodiscard]] bool remove_panel_from_tab_(DockNode* tab_node, std::string_view panel_id) noexcept;

    /// Walk drawing -- separate from `draw()` so it can recurse cleanly.
    void draw_node_(const DockNode* node,
                    cd::ui::renderer::DrawBatcher& batcher,
                    cd::ui::font::Font* font,
                    const Theme& theme) const;

    /// Walk for serialize/restore.
    void serialize_node_(const DockNode* node, std::vector<std::byte>& out) const;
    /// Returns nullptr on framing failure. `pos` is advanced on success.
    [[nodiscard]] std::unique_ptr<DockNode> deserialize_node_(
        std::span<const std::byte> bytes, std::size_t& pos) const;

    /// O(N) node count helper.
    [[nodiscard]] static std::size_t node_count_(const DockNode* n) noexcept;
};

// ---- Factory helpers (used by tests / scenes) -----------------------------

/// Build a single-leaf dock tree with one tab group containing `panel_id`.
[[nodiscard]] std::unique_ptr<DockNode> make_single_leaf(std::string panel_id);

/// Build a tab-group node with the supplied panel ids (must be non-empty).
[[nodiscard]] std::unique_ptr<DockNode> make_tab_group(std::vector<std::string> panel_ids);

/// Build a split node with two children + an axis + a ratio. Children are
/// moved-in; nullptr children yield empty kLeaf placeholders.
[[nodiscard]] std::unique_ptr<DockNode> make_split(DockAxis axis,
                                                   std::unique_ptr<DockNode> first,
                                                   std::unique_ptr<DockNode> second,
                                                   float ratio = 0.5F);

}  // namespace cd::ui::widgets
