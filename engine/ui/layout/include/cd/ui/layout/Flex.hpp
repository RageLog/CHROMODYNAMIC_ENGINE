// =============================================================================
// CHROMODYNAMIC — cd/ui/layout/Flex.hpp
//
// Phase 1.0 of ADR-20260530-ui-widget-library. Yoga-shape flexbox layout
// solver for retained-mode widgets. Pure C++ (no RHI, no font, no input);
// produces final pixel rectangles from style + parent constraints.
//
// Scope (Phase 1):
//   * Single-axis main direction (row / row-reverse / column / column-reverse)
//   * justify-content (flex-start / center / flex-end / space-between /
//                      space-around / space-evenly)
//   * align-items (flex-start / center / flex-end / stretch)
//   * gap (main + cross), padding (4 sides), margin (4 sides)
//   * flex-grow / flex-shrink (Yoga semantics: distribute remaining /
//                              shrink overshoot proportionally)
//   * fixed width / height OR auto (intrinsic content sizes from children)
//   * Absolute mode: explicit top/left/right/bottom override the flex flow
//
// Out of Phase 1:
//   * Wrap (flex-wrap: wrap) -- Phase 2
//   * Grid -- Phase 3
//   * Constraint solver (Cassowary-style) -- Phase 4
//   * BiDi RTL layout flip -- Phase 5
//
// Design notes:
//   * Saf-math: no allocations in the hot path other than `std::vector`
//     for children indices. Solver runs in O(N) per pass over a node tree.
//   * Header-only public types; impl in Flex.cpp keeps inline expansion
//     bounded.
//   * Yoga semantic parity at the algorithm level: callers familiar with
//     Yoga / Taffy can port styles directly. The unit-test harness
//     (test_flex.cpp) cross-validates ~30 cases against the Yoga
//     reference output documented in the test file.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace cd::ui::layout
{

// ---- Style enums -----------------------------------------------------------

enum class FlexDirection : std::uint8_t
{
    kRow         = 0,  ///< main axis = X, children laid out left-to-right
    kRowReverse  = 1,  ///< main axis = X, children laid out right-to-left
    kColumn      = 2,  ///< main axis = Y, children laid out top-to-bottom
    kColumnReverse = 3,
};

enum class JustifyContent : std::uint8_t
{
    kFlexStart    = 0,  ///< pack at main-axis start
    kCenter       = 1,
    kFlexEnd      = 2,  ///< pack at main-axis end
    kSpaceBetween = 3,  ///< first / last on edges, gaps even between
    kSpaceAround  = 4,  ///< half-gap on edges, full-gap between
    kSpaceEvenly  = 5,  ///< full-gap on edges and between
};

enum class AlignItems : std::uint8_t
{
    kFlexStart = 0,
    kCenter    = 1,
    kFlexEnd   = 2,
    kStretch   = 3,     ///< cross-axis size matches parent (minus padding)
};

enum class PositionType : std::uint8_t
{
    kRelative = 0,      ///< participates in flex flow
    kAbsolute = 1,      ///< removed from flow; placed by top/left/right/bottom
};

// ---- Style description ----------------------------------------------------

/// Edge-set: top / right / bottom / left (CSS order). Use named members
/// to avoid index-mistakes in callers.
struct EdgeInsets
{
    float top    { 0.0F };
    float right  { 0.0F };
    float bottom { 0.0F };
    float left   { 0.0F };
};

/// Sentinel "auto" -- caller didn't pin this dimension; solver derives
/// it from intrinsic size + grow/shrink. Encoded as a NaN so we don't
/// have to thread a separate `bool has_*` flag everywhere.
inline constexpr float kAuto = -1.0F;

/// Per-node style. Default-constructed = "row-flex, fill parent main,
/// stretch cross, no padding / margin / gap". A child node with default
/// style behaves like Yoga's `flex: 1; flex-direction: row; align-items:
/// stretch;`.
struct FlexStyle
{
    FlexDirection  direction      { FlexDirection::kRow };
    JustifyContent justify        { JustifyContent::kFlexStart };
    AlignItems     align_items    { AlignItems::kStretch };
    PositionType   position       { PositionType::kRelative };

    float          width          { kAuto };   ///< absolute width  in px, or kAuto
    float          height         { kAuto };   ///< absolute height in px, or kAuto
    float          min_width      { 0.0F };
    float          min_height     { 0.0F };
    float          max_width      { kAuto };   ///< kAuto = unbounded
    float          max_height     { kAuto };

    float          flex_grow      { 0.0F };    ///< Yoga semantics
    float          flex_shrink    { 1.0F };    ///< Yoga default
    float          flex_basis     { kAuto };

    EdgeInsets     margin         {};
    EdgeInsets     padding        {};

    float          gap_main       { 0.0F };    ///< gap along main axis
    float          gap_cross      { 0.0F };

    /// kAbsolute only: each side measured from the parent's inner edge.
    float          inset_top      { kAuto };
    float          inset_right    { kAuto };
    float          inset_bottom   { kAuto };
    float          inset_left     { kAuto };

    /// Intrinsic content size hint. Used for leaf nodes (text / image)
    /// where the renderer knows the natural extent. kAuto = no hint
    /// (the node behaves like an empty container).
    float          intrinsic_width  { kAuto };
    float          intrinsic_height { kAuto };
};

// ---- Tree node + computed layout ------------------------------------------

/// Solver-managed node id. Stable through one solver pass; index into the
/// `FlexTree::nodes_` array. NOT a hashable / persistent handle.
struct NodeId
{
    std::uint32_t value { 0U };
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0xFFFFFFFFu; }
    [[nodiscard]] constexpr bool operator==(NodeId other) const noexcept { return value == other.value; }
};

inline constexpr NodeId kInvalidNode { 0xFFFFFFFFu };

/// Final computed rect for one node. Origin = top-left of the ROOT's
/// coordinate frame (world / window pixels) so the renderer / hit-test
/// walker can iterate `layout(node)` over every node and skip the
/// parent-stack walk. The solver internally folds parent positions into
/// each child's rect during the recursive pass.
struct Rect
{
    float x      { 0.0F };
    float y      { 0.0F };
    float width  { 0.0F };
    float height { 0.0F };
};

/// Flat tree owned by the solver. `create_node` returns NodeId; `add_child`
/// links parent->child; `solve` writes computed Rects into the layout vector.
class FlexTree
{
public:
    FlexTree() = default;
    ~FlexTree() = default;

    FlexTree(const FlexTree&) = delete;
    FlexTree& operator=(const FlexTree&) = delete;
    FlexTree(FlexTree&&) noexcept = default;
    FlexTree& operator=(FlexTree&&) noexcept = default;

    /// Allocate a node with the given style. Returns the node's id.
    [[nodiscard]] NodeId create_node(const FlexStyle& style);

    /// Attach `child` as the last child of `parent`. Order matters --
    /// flex layout is sequential along the main axis.
    void add_child(NodeId parent, NodeId child);

    /// Mutate an existing node's style. Solver picks up the change on
    /// the next `solve` call.
    void set_style(NodeId node, const FlexStyle& style);

    /// Read computed layout for a node. Returns a zero-rect when the
    /// solver has never been run for this node.
    [[nodiscard]] Rect layout(NodeId node) const noexcept;

    /// Run the layout solver. The root receives `available_width` /
    /// `available_height` as its outer rectangle. Pixel values, NOT
    /// normalised. After return, `layout(node)` is populated for every
    /// node in the tree.
    void solve(NodeId root, float available_width, float available_height);

    /// Number of nodes currently in the tree.
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

private:
    struct InternalNode
    {
        FlexStyle               style;
        std::vector<NodeId>     children;
        Rect                    computed_rect {};
    };

    std::vector<InternalNode> nodes_;

    [[nodiscard]] InternalNode& node(NodeId id) { return nodes_[id.value]; }
    [[nodiscard]] const InternalNode& node(NodeId id) const { return nodes_[id.value]; }

    void solve_subtree(NodeId node, float w, float h);
    void compute_main(NodeId node, float main_available);
    void compute_cross(NodeId node, float cross_available);
    void position_children(NodeId node);
};

}  // namespace cd::ui::layout
