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
    return metas_.find(widget_id) != metas_.end();
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
[[nodiscard]] std::size_t index_of(std::span<const WidgetId> order, WidgetId id) noexcept
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

}  // namespace

void A11yTree::focus_next()
{
    if (tab_order_.empty())
    {
        return;
    }

    std::size_t next_idx = 0;
    if (focused_)
    {
        const std::size_t cur = index_of(tab_order_, *focused_);
        if (cur < tab_order_.size())
        {
            next_idx = (cur + 1) % tab_order_.size();
        }
    }

    set_focus(tab_order_[next_idx]);
}

void A11yTree::focus_prev()
{
    if (tab_order_.empty())
    {
        return;
    }

    std::size_t prev_idx = tab_order_.size() - 1;
    if (focused_)
    {
        const std::size_t cur = index_of(tab_order_, *focused_);
        if (cur < tab_order_.size())
        {
            // Wrap: 0 -> last.
            prev_idx = (cur == 0) ? (tab_order_.size() - 1) : (cur - 1);
        }
    }

    set_focus(tab_order_[prev_idx]);
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

}  // namespace cd::ui::a11y
