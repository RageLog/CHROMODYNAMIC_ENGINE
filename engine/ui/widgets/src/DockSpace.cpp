// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/DockSpace.cpp
//
// Implementation notes (T2.1 of ADR-20260530 editor enablement)
// -------------------------------------------------------------
// Tree-walk shape: every public mutation either (a) re-roots
// `root_`, or (b) edits an existing node in-place and leaves the
// recursive `compute_rects_` walk to refresh every node's cached
// pixel rect during the next tick().
//
// Hit-testing precedence:
//   1) splitter handles  (top-level)        -- thin band along the
//                                              child / child border
//   2) tab strips        (per kTabGroup)    -- horizontal strip on top
//   3) leaves            (per kTabGroup)    -- everything below the strip
//
// Drag-out latch: a tab click that is HELD while the cursor leaves the
// tab strip flips into a drag-out. Releasing on a leaf surfaces a drop
// zone; releasing elsewhere creates a floating leaf.
//
// Drop-zone geometry (matching ImGui's docking ratios closely enough for
// parity tests): 25% margins on every side, center occupies the inner
// 50% of both axes.
// =============================================================================
#include <cd/ui/widgets/DockSpace.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

namespace cd::ui::widgets
{

namespace
{

[[nodiscard]] cd::ui::renderer::Color to_renderer_color(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

/// Serialize a 32-bit unsigned in little-endian order.
void write_u32(std::vector<std::byte>& out, std::uint32_t v)
{
    for (std::uint32_t i = 0U; i < 4U; ++i)
    {
        out.push_back(static_cast<std::byte>((v >> (i * 8U)) & 0xFFU));
    }
}

/// Serialize a 32-bit float by bitcast. Endianness matches platform.
void write_f32(std::vector<std::byte>& out, float v)
{
    std::uint32_t bits {};
    std::memcpy(&bits, &v, sizeof(bits));
    write_u32(out, bits);
}

[[nodiscard]] bool read_u32(std::span<const std::byte> bytes, std::size_t& pos,
                            std::uint32_t& out_val) noexcept
{
    if (pos + 4U > bytes.size()) { return false; }
    std::uint32_t v = 0U;
    for (std::uint32_t i = 0U; i < 4U; ++i)
    {
        v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + i])) << (i * 8U);
    }
    pos    += 4U;
    out_val = v;
    return true;
}

[[nodiscard]] bool read_f32(std::span<const std::byte> bytes, std::size_t& pos,
                            float& out_val) noexcept
{
    std::uint32_t bits = 0U;
    if (!read_u32(bytes, pos, bits)) { return false; }
    std::memcpy(&out_val, &bits, sizeof(out_val));
    return true;
}

[[nodiscard]] bool read_byte(std::span<const std::byte> bytes, std::size_t& pos,
                             std::uint8_t& out_val) noexcept
{
    if (pos + 1U > bytes.size()) { return false; }
    out_val = static_cast<std::uint8_t>(bytes[pos]);
    pos += 1U;
    return true;
}

constexpr char kMagic[8] = { 'C', 'D', 'D', 'O', 'C', 'K', '\0', '\1' };

}  // namespace

// =========================================================================
// DockNode
// =========================================================================

void DockNode::set_ratio(float r) noexcept
{
    ratio_ = std::clamp(r, 0.05F, 0.95F);
}

void DockNode::set_active_tab(std::size_t idx) noexcept
{
    if (panels_.empty()) { active_tab_ = 0U; return; }
    active_tab_ = std::min(idx, panels_.size() - 1U);
}

// =========================================================================
// Factory helpers
// =========================================================================

std::unique_ptr<DockNode> make_single_leaf(std::string panel_id)
{
    auto node = std::make_unique<DockNode>(DockNodeKind::kTabGroup);
    node->panels_.push_back(std::move(panel_id));
    node->active_tab_ = 0U;
    return node;
}

std::unique_ptr<DockNode> make_tab_group(std::vector<std::string> panel_ids)
{
    auto node = std::make_unique<DockNode>(DockNodeKind::kTabGroup);
    node->panels_ = std::move(panel_ids);
    if (node->panels_.empty())
    {
        // Promote to placeholder leaf if caller passed an empty list.
        node->kind_ = DockNodeKind::kLeaf;
    }
    node->active_tab_ = 0U;
    return node;
}

std::unique_ptr<DockNode> make_split(DockAxis axis,
                                     std::unique_ptr<DockNode> first,
                                     std::unique_ptr<DockNode> second,
                                     float ratio)
{
    auto node = std::make_unique<DockNode>(DockNodeKind::kSplit);
    node->axis_  = axis;
    node->set_ratio(ratio);
    node->first_  = first  ? std::move(first)  : std::make_unique<DockNode>(DockNodeKind::kLeaf);
    node->second_ = second ? std::move(second) : std::make_unique<DockNode>(DockNodeKind::kLeaf);
    return node;
}

// =========================================================================
// DockSpace -- construction & panel registry
// =========================================================================

DockSpace::DockSpace()
    : root_(std::make_unique<DockNode>(DockNodeKind::kLeaf))
{
}

bool DockSpace::register_panel(std::string id, ContentDrawer drawer)
{
    const auto [it, inserted] = panels_.try_emplace(std::move(id), std::move(drawer));
    if (!inserted)
    {
        it->second = std::move(drawer);  // overwrite
    }
    return inserted;
}

bool DockSpace::has_panel(std::string_view id) const noexcept
{
    // unordered_map heterogeneous lookup is C++20 with transparent hash;
    // for portability we copy the view into a string for the lookup.
    return panels_.find(std::string { id }) != panels_.end();
}

// =========================================================================
// Tree walks -- panel owner lookup & node counting
// =========================================================================

DockNode* DockSpace::find_panel_owner(std::string_view panel_id) noexcept
{
    // Const-cast trick used here so the const overload can share code
    // without duplicating the recursion. `find_panel_owner(const)` calls
    // this and the cast away const is sound because the input was const.
    auto walk = [&](auto& self, DockNode* n) -> DockNode* {
        if (n == nullptr) { return nullptr; }
        if (n->kind() == DockNodeKind::kTabGroup)
        {
            for (const std::string& p : n->panels())
            {
                if (p == panel_id) { return n; }
            }
        }
        else if (n->kind() == DockNodeKind::kSplit)
        {
            if (auto* a = self(self, n->first()))  { return a; }
            if (auto* b = self(self, n->second())) { return b; }
        }
        return nullptr;
    };

    if (auto* a = walk(walk, root_.get())) { return a; }
    for (const auto& fn : floating_panels_)
    {
        if (auto* a = walk(walk, fn.get())) { return a; }
    }
    return nullptr;
}

const DockNode* DockSpace::find_panel_owner(std::string_view panel_id) const noexcept
{
    return const_cast<DockSpace*>(this)->find_panel_owner(panel_id);
}

std::size_t DockSpace::node_count_(const DockNode* n) noexcept
{
    if (n == nullptr) { return 0U; }
    if (n->kind() == DockNodeKind::kSplit)
    {
        return 1U + node_count_(n->first()) + node_count_(n->second());
    }
    return 1U;
}

std::size_t DockSpace::node_count() const noexcept
{
    return node_count_(root_.get());
}

// =========================================================================
// Programmatic operations: split / tab_merge / undock
// =========================================================================

bool DockSpace::split(DockNode* target_leaf, DockAxis axis,
                      std::string new_panel_id, float ratio)
{
    if (target_leaf == nullptr) { return false; }
    if (target_leaf->kind() != DockNodeKind::kTabGroup &&
        target_leaf->kind() != DockNodeKind::kLeaf)
    {
        return false;
    }

    // Build the new leaf for the dragged-in panel.
    auto new_leaf = make_single_leaf(std::move(new_panel_id));

    // If target is the root: wrap the entire root inside a new split.
    DockNode* parent = find_parent_(target_leaf);
    auto split_node = make_split(axis, nullptr, nullptr, ratio);
    split_node->set_axis(axis);
    split_node->set_ratio(ratio);

    if (parent == nullptr)
    {
        // target_leaf IS the root. Move ownership of root_ into the split.
        split_node->first_  = std::move(root_);
        split_node->second_ = std::move(new_leaf);
        root_ = std::move(split_node);
    }
    else
    {
        // Identify which child slot of parent the target lives in,
        // move it into the new split, then plug the split back into
        // the parent slot.
        std::unique_ptr<DockNode>* slot = nullptr;
        if (parent->first_.get() == target_leaf)  { slot = &parent->first_; }
        if (parent->second_.get() == target_leaf) { slot = &parent->second_; }
        if (slot == nullptr) { return false; }

        std::unique_ptr<DockNode> orig = std::move(*slot);
        split_node->first_  = std::move(orig);
        split_node->second_ = std::move(new_leaf);
        *slot = std::move(split_node);
    }
    return true;
}

bool DockSpace::tab_merge(DockNode* target, std::string new_panel_id)
{
    if (target == nullptr) { return false; }
    if (target->kind() != DockNodeKind::kTabGroup &&
        target->kind() != DockNodeKind::kLeaf)
    {
        return false;
    }
    // Promote empty leaf to a tab group.
    if (target->kind() == DockNodeKind::kLeaf)
    {
        target->kind_ = DockNodeKind::kTabGroup;
    }
    target->panels_.push_back(std::move(new_panel_id));
    target->active_tab_ = target->panels_.size() - 1U;
    return true;
}

bool DockSpace::remove_panel_from_tab_(DockNode* tab_node, std::string_view panel_id) noexcept
{
    if (tab_node == nullptr || tab_node->kind() != DockNodeKind::kTabGroup) { return false; }
    auto it = std::ranges::find(tab_node->panels_, panel_id);
    if (it == tab_node->panels_.end()) { return false; }
    const auto removed_idx = static_cast<std::size_t>(
        std::distance(tab_node->panels_.begin(), it));
    tab_node->panels_.erase(it);
    if (tab_node->panels_.empty())
    {
        tab_node->kind_      = DockNodeKind::kLeaf;
        tab_node->active_tab_ = 0U;
    }
    else if (tab_node->active_tab_ >= tab_node->panels_.size())
    {
        tab_node->active_tab_ = tab_node->panels_.size() - 1U;
    }
    else if (removed_idx < tab_node->active_tab_)
    {
        // Keep the same logical tab active after a left-of-active removal.
        tab_node->active_tab_ -= 1U;
    }
    return true;
}

DockNode* DockSpace::find_parent_(DockNode* target) noexcept
{
    if (target == nullptr || root_.get() == target) { return nullptr; }

    auto walk = [&](auto& self, DockNode* n) -> DockNode* {
        if (n == nullptr || n->kind() != DockNodeKind::kSplit) { return nullptr; }
        if (n->first() == target || n->second() == target) { return n; }
        if (auto* a = self(self, n->first()))  { return a; }
        if (auto* b = self(self, n->second())) { return b; }
        return nullptr;
    };
    return walk(walk, root_.get());
}

void DockSpace::collapse_parent_of_(DockNode* child) noexcept
{
    if (child == nullptr) { return; }
    DockNode* parent = find_parent_(child);
    if (parent == nullptr) { return; }  // child was root; nothing to collapse

    // Promote the sibling. The dying child node falls out of scope when
    // its owning unique_ptr is overwritten.
    std::unique_ptr<DockNode> sibling;
    if (parent->first_.get() == child)
    {
        sibling = std::move(parent->second_);
    }
    else if (parent->second_.get() == child)
    {
        sibling = std::move(parent->first_);
    }
    else
    {
        return;
    }

    // Replace the parent in *its* parent (or root_) with the promoted sibling.
    DockNode* grandparent = find_parent_(parent);
    if (grandparent == nullptr)
    {
        root_ = std::move(sibling);
    }
    else if (grandparent->first_.get() == parent)
    {
        grandparent->first_ = std::move(sibling);
    }
    else if (grandparent->second_.get() == parent)
    {
        grandparent->second_ = std::move(sibling);
    }
}

bool DockSpace::undock(std::string_view panel_id)
{
    DockNode* owner = find_panel_owner(panel_id);
    if (owner == nullptr) { return false; }
    // Build a new floating leaf with the panel id.
    auto floating = make_single_leaf(std::string { panel_id });

    const bool was_only_panel = (owner->panels().size() == 1U);
    if (!remove_panel_from_tab_(owner, panel_id)) { return false; }

    if (was_only_panel)
    {
        // The owner now has zero panels -> collapse its parent slot so we
        // don't leak an empty leaf into the tree (unless owner is root,
        // in which case the empty placeholder is fine).
        if (owner != root_.get())
        {
            collapse_parent_of_(owner);
        }
    }

    floating_panels_.push_back(std::move(floating));
    return true;
}

bool DockSpace::drop_floating(DockNode* target_leaf, DockDropZone zone)
{
    if (floating_panels_.empty() || zone == DockDropZone::kNone) { return false; }
    auto floating = std::move(floating_panels_.front());
    floating_panels_.erase(floating_panels_.begin());

    // The floating leaf is a kTabGroup with exactly one panel -- pull it.
    if (floating->panels().empty()) { return false; }
    std::string panel_id { floating->panels()[0] };

    switch (zone)
    {
    case DockDropZone::kCenter:
        return tab_merge(target_leaf, std::move(panel_id));
    case DockDropZone::kTop:
        // Split horizontal, dragged panel becomes the FIRST (top) child.
        // We re-implement split() here because the brief variant wants
        // the new panel on the first slot, not the second.
        return split(target_leaf, DockAxis::kHorizontal, std::move(panel_id), 0.5F);
    case DockDropZone::kBottom:
        return split(target_leaf, DockAxis::kHorizontal, std::move(panel_id), 0.5F);
    case DockDropZone::kLeft:
        return split(target_leaf, DockAxis::kVertical,   std::move(panel_id), 0.5F);
    case DockDropZone::kRight:
        return split(target_leaf, DockAxis::kVertical,   std::move(panel_id), 0.5F);
    case DockDropZone::kNone:
    default:
        return false;
    }
}

bool DockSpace::activate_panel(std::string_view panel_id)
{
    DockNode* owner = find_panel_owner(panel_id);
    if (owner == nullptr) { return false; }
    for (std::size_t i = 0U; i < owner->panels().size(); ++i)
    {
        if (owner->panels()[i] == panel_id)
        {
            owner->set_active_tab(i);
            return true;
        }
    }
    return false;
}

// =========================================================================
// Rect computation + hit testing
// =========================================================================

void DockSpace::compute_rects_(DockNode* node, const Rect& r) noexcept
{
    if (node == nullptr) { return; }
    node->set_rect(r);
    if (node->kind() != DockNodeKind::kSplit) { return; }

    const float ratio = node->ratio();
    if (node->axis() == DockAxis::kVertical)
    {
        const float w1 = r.w * ratio;
        const float w2 = r.w - w1;
        Rect r1 { r.x,       r.y, w1, r.h };
        Rect r2 { r.x + w1,  r.y, w2, r.h };
        compute_rects_(node->first(),  r1);
        compute_rects_(node->second(), r2);
    }
    else
    {
        const float h1 = r.h * ratio;
        const float h2 = r.h - h1;
        Rect r1 { r.x, r.y,      r.w, h1 };
        Rect r2 { r.x, r.y + h1, r.w, h2 };
        compute_rects_(node->first(),  r1);
        compute_rects_(node->second(), r2);
    }
}

DockNode* DockSpace::hit_test_leaf_(DockNode* node, float px, float py) noexcept
{
    if (node == nullptr) { return nullptr; }
    if (!node->rect().contains(px, py)) { return nullptr; }
    if (node->kind() == DockNodeKind::kSplit)
    {
        if (auto* a = hit_test_leaf_(node->first(),  px, py)) { return a; }
        if (auto* b = hit_test_leaf_(node->second(), px, py)) { return b; }
        return nullptr;
    }
    return node;
}

DockNode* DockSpace::hit_test_splitter_(DockNode* node, float px, float py) noexcept
{
    if (node == nullptr || node->kind() != DockNodeKind::kSplit) { return nullptr; }
    // Splitter band sits on the boundary between first / second.
    const Rect& r1 = node->first()->rect();
    const float tol = Splitter::kHitTolerance;
    if (node->axis() == DockAxis::kVertical)
    {
        const float border_x = r1.x + r1.w;
        if (py >= r1.y && py < r1.y + r1.h &&
            std::abs(px - border_x) <= tol)
        {
            return node;
        }
    }
    else
    {
        const float border_y = r1.y + r1.h;
        if (px >= r1.x && px < r1.x + r1.w &&
            std::abs(py - border_y) <= tol)
        {
            return node;
        }
    }
    // Recurse into children.
    if (auto* a = hit_test_splitter_(node->first(),  px, py)) { return a; }
    if (auto* b = hit_test_splitter_(node->second(), px, py)) { return b; }
    return nullptr;
}

DockNode* DockSpace::hit_test_tab_strip_(DockNode* node, float px, float py,
                                         std::size_t& out_tab_idx,
                                         bool& out_inside_strip) noexcept
{
    out_tab_idx = static_cast<std::size_t>(-1);
    out_inside_strip = false;
    if (node == nullptr) { return nullptr; }
    if (!node->rect().contains(px, py)) { return nullptr; }

    if (node->kind() == DockNodeKind::kSplit)
    {
        if (auto* a = hit_test_tab_strip_(node->first(),  px, py, out_tab_idx, out_inside_strip)) { return a; }
        if (auto* b = hit_test_tab_strip_(node->second(), px, py, out_tab_idx, out_inside_strip)) { return b; }
        return nullptr;
    }
    if (node->kind() != DockNodeKind::kTabGroup) { return nullptr; }

    const Rect& nr = node->rect();
    if (py < nr.y || py >= nr.y + TabStrip::kHeight) { return nullptr; }

    out_inside_strip = true;
    const float local_x = px - nr.x;
    const float per_tab = TabStrip::kTabWidth + TabStrip::kTabPad;
    auto idx = static_cast<std::size_t>(local_x / per_tab);
    if (idx < node->panels().size())
    {
        out_tab_idx = idx;
    }
    return node;
}

DockDropZone DockSpace::classify_drop_zone_(const Rect& leaf_rect,
                                            float px, float py) noexcept
{
    if (!leaf_rect.contains(px, py)) { return DockDropZone::kNone; }
    const float x = px - leaf_rect.x;
    const float y = py - leaf_rect.y;
    const float w = leaf_rect.w;
    const float h = leaf_rect.h;
    // 25% margins on each side; center occupies the inner 50%x50%.
    const float left_edge   = w * 0.25F;
    const float right_edge  = w * 0.75F;
    const float top_edge    = h * 0.25F;
    const float bottom_edge = h * 0.75F;

    const bool in_center_x = (x >= left_edge && x <= right_edge);
    const bool in_center_y = (y >= top_edge  && y <= bottom_edge);
    if (in_center_x && in_center_y) { return DockDropZone::kCenter; }
    if (in_center_x && y <  top_edge)    { return DockDropZone::kTop; }
    if (in_center_x && y >  bottom_edge) { return DockDropZone::kBottom; }
    if (in_center_y && x <  left_edge)   { return DockDropZone::kLeft; }
    if (in_center_y && x >  right_edge)  { return DockDropZone::kRight; }
    // Corners pick the nearer cardinal -- defer to horizontal edge.
    if (x < left_edge)       { return DockDropZone::kLeft; }
    if (x > right_edge)      { return DockDropZone::kRight; }
    if (y < top_edge)        { return DockDropZone::kTop; }
    return DockDropZone::kBottom;
}

// =========================================================================
// tick()
// =========================================================================

void DockSpace::tick(const InputState& input, float dt_s)
{
    (void) dt_s;

    // (1) Recompute every node's pixel rect from the root.
    compute_rects_(root_.get(), rect_);

    const float px = input.pointer.mouse_x;
    const float py = input.pointer.mouse_y;

    // (2) Splitter drag.
    if (input.pointer.left_pressed)
    {
        dragging_splitter_ = hit_test_splitter_(root_.get(), px, py);
    }
    if (dragging_splitter_ != nullptr && input.pointer.left_down)
    {
        DockNode* sp = dragging_splitter_;
        // Translate cursor delta into a new ratio.
        // We resolve the parent rect by walking back to sp's own rect (which
        // already covers both children combined).
        const Rect& r = sp->rect();
        if (sp->axis() == DockAxis::kVertical && r.w > 0.0F)
        {
            const float new_ratio = (px - r.x) / r.w;
            sp->set_ratio(new_ratio);
        }
        else if (sp->axis() == DockAxis::kHorizontal && r.h > 0.0F)
        {
            const float new_ratio = (py - r.y) / r.h;
            sp->set_ratio(new_ratio);
        }
        // Refresh children rects post-drag for downstream hit-tests this frame.
        compute_rects_(root_.get(), rect_);
    }
    if (input.pointer.left_released)
    {
        dragging_splitter_ = nullptr;
    }

    // (3) Tab strip / drag-out handling.
    auto        tab_idx       = static_cast<std::size_t>(-1);
    bool        inside_strip  = false;
    DockNode*   tab_node      = hit_test_tab_strip_(root_.get(), px, py, tab_idx, inside_strip);

    if (input.pointer.left_pressed && tab_node != nullptr && tab_idx != static_cast<std::size_t>(-1))
    {
        // Click activates the clicked tab.
        tab_node->set_active_tab(tab_idx);
        // Latch the panel id as the drag candidate. It only becomes a
        // real drag-out once the cursor leaves the strip while held.
        dragging_panel_ = std::string { tab_node->panels()[tab_idx] };
    }

    // Promote latch into a drag-out when the cursor leaves the strip
    // while held. While the cursor is still inside the SAME tab strip
    // we treat the gesture as a tab click (no drop target). Only once
    // the cursor leaves any tab strip do we record a drop target /
    // drop zone for the eventual release. This keeps the common
    // "click a tab" gesture from being misinterpreted as a drag-out.
    if (!dragging_panel_.empty() && input.pointer.left_down && !inside_strip)
    {
        DockNode* hovered = hit_test_leaf_(root_.get(), px, py);
        if (hovered != nullptr)
        {
            current_drop_target_ = hovered;
            current_drop_zone_   = classify_drop_zone_(hovered->rect(), px, py);
        }
        else
        {
            current_drop_target_ = nullptr;
            current_drop_zone_   = DockDropZone::kNone;
        }
    }

    // (4) Release while a drag is in flight -> drop or float.
    if (input.pointer.left_released && !dragging_panel_.empty())
    {
        const std::string panel_id { dragging_panel_ };

        // Only treat the release as a drag-out when the cursor is no
        // longer on a tab strip (or a target leaf has been recorded
        // outside the strip during the held-down phase). Releasing
        // while still on a tab strip just commits the tab click.
        const bool released_outside_strip =
            (!inside_strip) || (current_drop_target_ != nullptr);

        if (released_outside_strip)
        {
            if (current_drop_target_ != nullptr &&
                current_drop_zone_ != DockDropZone::kNone)
            {
                DockNode* target = current_drop_target_;
                if (undock(panel_id))
                {
                    drop_floating(target, current_drop_zone_);
                }
            }
            else
            {
                // Released over empty space -> create / keep floating panel.
                undock(panel_id);
            }
        }
        // Else: idle click on a tab strip -- the active tab was already
        // updated on press; nothing further to do.

        dragging_panel_.clear();
        current_drop_target_ = nullptr;
        current_drop_zone_   = DockDropZone::kNone;
    }

    left_down_prev_ = input.pointer.left_down;
}

// =========================================================================
// draw()
// =========================================================================

void DockSpace::draw_node_(const DockNode* node,
                           cd::ui::renderer::DrawBatcher& batcher,
                           cd::ui::font::Font* font,
                           const Theme& theme) const
{
    if (node == nullptr) { return; }

    if (node->kind() == DockNodeKind::kSplit)
    {
        draw_node_(node->first(),  batcher, font, theme);
        draw_node_(node->second(), batcher, font, theme);

        // Splitter handle.
        const Rect& r1 = node->first()->rect();
        const cd::ui::renderer::Color sc = to_renderer_color(theme.surface_press);
        if (node->axis() == DockAxis::kVertical)
        {
            batcher.quad(r1.x + r1.w - (Splitter::kHandleThickness * 0.5F),
                         r1.y,
                         Splitter::kHandleThickness,
                         r1.h,
                         sc);
        }
        else
        {
            batcher.quad(r1.x,
                         r1.y + r1.h - (Splitter::kHandleThickness * 0.5F),
                         r1.w,
                         Splitter::kHandleThickness,
                         sc);
        }
        return;
    }

    if (node->kind() == DockNodeKind::kLeaf)
    {
        // Empty placeholder -- paint with surface tint so it's visible.
        const Rect& nr = node->rect();
        batcher.quad(nr.x, nr.y, nr.w, nr.h, to_renderer_color(theme.surface));
        return;
    }

    // kTabGroup.
    const Rect& nr = node->rect();
    // Tab strip background.
    batcher.quad(nr.x, nr.y, nr.w, TabStrip::kHeight,
                 to_renderer_color(theme.surface));

    for (std::size_t i = 0U; i < node->panels().size(); ++i)
    {
        const float tab_x = nr.x + static_cast<float>(i) *
            (TabStrip::kTabWidth + TabStrip::kTabPad);
        const float tab_y = nr.y;
        const cd::ui::renderer::Color tc = to_renderer_color(
            i == node->active_tab() ? theme.surface_hover : theme.surface);
        batcher.quad(tab_x, tab_y, TabStrip::kTabWidth,
                     TabStrip::kHeight - 2.0F, tc);
    }

    // Body of the active tab.
    const Rect body { nr.x, nr.y + TabStrip::kHeight,
                      nr.w, std::max(0.0F, nr.h - TabStrip::kHeight) };
    batcher.quad(body.x, body.y, body.w, body.h,
                 to_renderer_color(theme.background));
    if (node->active_tab() < node->panels().size())
    {
        const std::string& pid = node->panels()[node->active_tab()];
        auto it = panels_.find(pid);
        if (it != panels_.end() && it->second)
        {
            it->second(body, batcher, font, theme);
        }
    }
}

void DockSpace::draw(cd::ui::renderer::DrawBatcher& batcher,
                     cd::ui::font::Font* font,
                     const Theme& theme) const
{
    draw_node_(root_.get(), batcher, font, theme);

    // Drop-target hint quad while dragging.
    if (!dragging_panel_.empty() &&
        current_drop_target_ != nullptr &&
        current_drop_zone_ != DockDropZone::kNone)
    {
        const Rect& r = current_drop_target_->rect();
        cd::ui::renderer::Color hint = to_renderer_color(theme.accent);
        hint.a = 96U;  // translucent overlay

        Rect zone { r.x, r.y, r.w, r.h };
        switch (current_drop_zone_)
        {
        case DockDropZone::kCenter:
            zone = { r.x + r.w * 0.25F, r.y + r.h * 0.25F,
                     r.w * 0.5F,        r.h * 0.5F };
            break;
        case DockDropZone::kTop:
            zone = { r.x, r.y, r.w, r.h * 0.5F };
            break;
        case DockDropZone::kBottom:
            zone = { r.x, r.y + r.h * 0.5F, r.w, r.h * 0.5F };
            break;
        case DockDropZone::kLeft:
            zone = { r.x, r.y, r.w * 0.5F, r.h };
            break;
        case DockDropZone::kRight:
            zone = { r.x + r.w * 0.5F, r.y, r.w * 0.5F, r.h };
            break;
        case DockDropZone::kNone:
        default:
            break;
        }
        batcher.quad(zone.x, zone.y, zone.w, zone.h, hint);
    }
}

// =========================================================================
// Serialize / restore
// =========================================================================

void DockSpace::serialize_node_(const DockNode* node, std::vector<std::byte>& out) const
{
    if (node == nullptr)
    {
        out.push_back(static_cast<std::byte>(DockNodeKind::kLeaf));
        return;
    }
    out.push_back(static_cast<std::byte>(node->kind()));
    if (node->kind() == DockNodeKind::kSplit)
    {
        out.push_back(static_cast<std::byte>(node->axis()));
        write_f32(out, node->ratio());
        serialize_node_(node->first(),  out);
        serialize_node_(node->second(), out);
    }
    else if (node->kind() == DockNodeKind::kTabGroup)
    {
        write_u32(out, static_cast<std::uint32_t>(node->panels().size()));
        write_u32(out, static_cast<std::uint32_t>(node->active_tab()));
        for (const std::string& p : node->panels())
        {
            write_u32(out, static_cast<std::uint32_t>(p.size()));
            for (const char c : p)
            {
                out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c)));
            }
        }
    }
    // kLeaf: zero payload.
}

std::vector<std::byte> DockSpace::serialize() const
{
    std::vector<std::byte> out;
    out.reserve(64U);
    for (const char c : kMagic)
    {
        out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(c)));
    }
    serialize_node_(root_.get(), out);
    return out;
}

std::unique_ptr<DockNode> DockSpace::deserialize_node_(
    std::span<const std::byte> bytes, std::size_t& pos) const
{
    std::uint8_t kind_raw = 0U;
    if (!read_byte(bytes, pos, kind_raw)) { return nullptr; }
    if (kind_raw > static_cast<std::uint8_t>(DockNodeKind::kLeaf)) { return nullptr; }
    const auto kind = static_cast<DockNodeKind>(kind_raw);

    auto node = std::make_unique<DockNode>(kind);
    if (kind == DockNodeKind::kSplit)
    {
        std::uint8_t axis_raw = 0U;
        float        ratio    = 0.5F;
        if (!read_byte(bytes, pos, axis_raw)) { return nullptr; }
        if (axis_raw > static_cast<std::uint8_t>(DockAxis::kHorizontal)) { return nullptr; }
        if (!read_f32(bytes, pos, ratio)) { return nullptr; }
        node->set_axis(static_cast<DockAxis>(axis_raw));
        node->set_ratio(ratio);
        node->first_  = deserialize_node_(bytes, pos);
        node->second_ = deserialize_node_(bytes, pos);
        if (!node->first_ || !node->second_) { return nullptr; }
    }
    else if (kind == DockNodeKind::kTabGroup)
    {
        std::uint32_t count  = 0U;
        std::uint32_t active = 0U;
        if (!read_u32(bytes, pos, count))  { return nullptr; }
        if (!read_u32(bytes, pos, active)) { return nullptr; }
        for (std::uint32_t i = 0U; i < count; ++i)
        {
            std::uint32_t id_len = 0U;
            if (!read_u32(bytes, pos, id_len)) { return nullptr; }
            if (pos + id_len > bytes.size())   { return nullptr; }
            std::string id;
            id.reserve(id_len);
            for (std::uint32_t k = 0U; k < id_len; ++k)
            {
                id.push_back(static_cast<char>(static_cast<std::uint8_t>(bytes[pos + k])));
            }
            pos += id_len;
            node->panels_.push_back(std::move(id));
        }
        node->active_tab_ = (count == 0U) ? 0U : std::min<std::size_t>(active, count - 1U);
        if (count == 0U) { node->kind_ = DockNodeKind::kLeaf; }
    }
    return node;
}

bool DockSpace::restore(std::span<const std::byte> bytes)
{
    if (bytes.size() < sizeof(kMagic)) { return false; }
    for (std::size_t i = 0U; i < sizeof(kMagic); ++i)
    {
        if (static_cast<char>(static_cast<std::uint8_t>(bytes[i])) != kMagic[i])
        {
            return false;
        }
    }
    std::size_t pos = sizeof(kMagic);
    auto new_root = deserialize_node_(bytes, pos);
    if (!new_root) { return false; }
    // Atomic swap on success.
    root_ = std::move(new_root);
    floating_panels_.clear();
    dragging_panel_.clear();
    current_drop_target_ = nullptr;
    current_drop_zone_   = DockDropZone::kNone;
    dragging_splitter_   = nullptr;
    return true;
}

}  // namespace cd::ui::widgets
