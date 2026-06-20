// =============================================================================
// CHROMODYNAMIC -- cd/ui/a11y/A11y.cpp
//
// Phase 478 -- non-trivial A11yTree members. The header carries the
// constexpr contrast helpers + focus_indicator_rect; this file holds
// the registration / lookup / tab-navigation logic.
// =============================================================================
#include <cd/ui/a11y/A11y.hpp>

#include <algorithm>
#include <utility>

namespace cd::ui::a11y
{

void A11yTree::register_widget(WidgetId widget_id, A11yMeta meta)
{
    // Idempotent overwrite: insert_or_assign returns iterator + bool, we
    // ignore the bool because retained-mode refreshes legitimately
    // re-register the same id with updated label/role.
    metas_.insert_or_assign(widget_id, std::move(meta));
}

bool A11yTree::update_meta(WidgetId widget_id, A11yMeta patch)
{
    const auto it = metas_.find(widget_id);
    if (it == metas_.end())
    {
        return false;
    }
    // Preserve the tree-managed focused bit; replace all other fields.
    const bool was_focused = it->second.focused;
    it->second = std::move(patch);
    it->second.focused = was_focused;
    return true;
}

void A11yTree::unregister_widget(WidgetId widget_id)
{
    metas_.erase(widget_id);

    // Drop from tab_order_ so focus_next() never lands on a removed id.
    const auto removed = std::ranges::remove(tab_order_, widget_id);
    tab_order_.erase(removed.begin(), removed.end());

    if (focused_ && *focused_ == widget_id)
    {
        focused_.reset();
    }

    // Remove from parent/child adjacency maps.
    // 1. If this widget was a child of some parent, remove it from that
    //    parent's children list.
    const auto parent_it = parents_.find(widget_id);
    if (parent_it != parents_.end())
    {
        const WidgetId parent_id = parent_it->second;
        const auto children_it   = children_.find(parent_id);
        if (children_it != children_.end())
        {
            const auto erased = std::ranges::remove(children_it->second, widget_id);
            children_it->second.erase(erased.begin(), erased.end());
        }
        parents_.erase(parent_it);
    }

    // 2. If this widget was a parent, clear the parent pointer of all its
    //    children so they become root-level (no parent).
    const auto children_it = children_.find(widget_id);
    if (children_it != children_.end())
    {
        for (const WidgetId child : children_it->second)
        {
            parents_.erase(child);
        }
        children_.erase(children_it);
    }
}

A11yMeta A11yTree::meta(WidgetId widget_id) const
{
    const auto it = metas_.find(widget_id);
    if (it == metas_.end())
    {
        return A11yMeta {};
    }
    return it->second;
}

bool A11yTree::has(WidgetId widget_id) const noexcept
{
    return metas_.contains(widget_id);
}

void A11yTree::set_tab_order(std::span<const WidgetId> order)
{
    tab_order_.assign(order.begin(), order.end());
}

void A11yTree::set_focus(std::optional<WidgetId> widget_id)
{
    // Clear the previous focused widget's per-meta bit (if any).
    if (focused_)
    {
        const auto it = metas_.find(*focused_);
        if (it != metas_.end())
        {
            it->second.focused = false;
        }
    }

    focused_ = widget_id;

    if (widget_id)
    {
        const auto it = metas_.find(*widget_id);
        if (it != metas_.end())
        {
            it->second.focused = true;
        }
    }
}

namespace
{

// Locate `id` in the tab order. Returns end-iterator semantics: index =
// size() when absent. Pulled out so focus_next / focus_prev share the
// same lookup.
[[nodiscard]] std::size_t index_of(std::span<const WidgetId> order,
                                   WidgetId                   id) noexcept
{
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        if (order[i] == id)
        {
            return i;
        }
    }
    return order.size();
}

// Returns true if the widget referenced by `id` is disabled (i.e. its meta
// has disabled == true). Unknown ids (not in metas) are NOT considered
// disabled so pre-baked tab orders still navigate through them.
[[nodiscard]] bool is_disabled(
    const std::unordered_map<WidgetId, A11yMeta>& metas,
    WidgetId                                       id) noexcept
{
    const auto it = metas.find(id);
    return (it != metas.end()) && it->second.disabled;
}

}  // namespace

void A11yTree::focus_next()
{
    if (tab_order_.empty())
    {
        return;
    }

    const std::size_t n   = tab_order_.size();
    std::size_t       cur = 0;
    if (focused_)
    {
        const std::size_t pos = index_of(tab_order_, *focused_);
        cur = (pos < n) ? ((pos + 1U) % n) : 0U;
    }

    // Walk forward (at most n steps) to find the first non-disabled entry.
    for (std::size_t step = 0; step < n; ++step)
    {
        const std::size_t idx = (cur + step) % n;
        if (!is_disabled(metas_, tab_order_[idx]))
        {
            set_focus(tab_order_[idx]);
            return;
        }
    }
    // All entries are disabled: leave focus unchanged.
}

void A11yTree::focus_prev()
{
    if (tab_order_.empty())
    {
        return;
    }

    const std::size_t n   = tab_order_.size();
    std::size_t       cur = n - 1U;
    if (focused_)
    {
        const std::size_t pos = index_of(tab_order_, *focused_);
        if (pos < n)
        {
            cur = (pos == 0U) ? (n - 1U) : (pos - 1U);
        }
    }

    // Walk backward (at most n steps) to find the first non-disabled entry.
    for (std::size_t step = 0; step < n; ++step)
    {
        // cur - step wraps: use (n + cur - step) % n to stay unsigned.
        const std::size_t idx = (n + cur - step) % n;
        if (!is_disabled(metas_, tab_order_[idx]))
        {
            set_focus(tab_order_[idx]);
            return;
        }
    }
    // All entries are disabled: leave focus unchanged.
}

std::string_view A11yTree::screen_reader_hint(WidgetId widget_id) const
{
    const auto it = metas_.find(widget_id);
    if (it == metas_.end())
    {
        return {};
    }
    return it->second.hint;
}

// ---- Nested-node adjacency -------------------------------------------------

void A11yTree::set_parent(WidgetId child_id, WidgetId parent_id)
{
    if (child_id == parent_id)
    {
        return;  // self-loop guard
    }

    // Remove from any previous parent's children list.
    const auto prev_it = parents_.find(child_id);
    if (prev_it != parents_.end())
    {
        const WidgetId prev_parent = prev_it->second;
        if (prev_parent != parent_id)
        {
            auto& prev_children = children_[prev_parent];
            const auto erased   = std::ranges::remove(prev_children, child_id);
            prev_children.erase(erased.begin(), erased.end());
        }
    }

    parents_.insert_or_assign(child_id, parent_id);
    children_[parent_id].emplace_back(child_id);
}

void A11yTree::clear_parent(WidgetId child_id)
{
    const auto it = parents_.find(child_id);
    if (it == parents_.end())
    {
        return;
    }
    const WidgetId parent_id    = it->second;
    auto&          parent_list  = children_[parent_id];
    const auto     erased       = std::ranges::remove(parent_list, child_id);
    parent_list.erase(erased.begin(), erased.end());
    parents_.erase(it);
}

std::optional<WidgetId> A11yTree::parent_of(WidgetId child_id) const
{
    const auto it = parents_.find(child_id);
    if (it == parents_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<WidgetId> A11yTree::children_of(WidgetId parent_id) const
{
    const auto it = children_.find(parent_id);
    if (it == children_.end())
    {
        return {};
    }
    return it->second;
}

}  // namespace cd::ui::a11y
