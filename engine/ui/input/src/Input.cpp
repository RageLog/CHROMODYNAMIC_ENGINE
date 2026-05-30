// =============================================================================
// CHROMODYNAMIC — cd/ui/input/Input.cpp
//
// Phase 2.1 implementation. See Input.hpp for the scope + API contract.
//
// Algorithm notes:
//   * HitTester::hit_test walks the rect list in reverse (last-wins for
//     z-order). Branchless point-in-rect check.
//   * FocusManager keeps `chain_` as the global tab order and one
//     `ModalFrame` per modal push. The active chain narrows when a modal
//     is on the stack; focus mutation paths all go through `active_chain_`
//     so the modal-capture invariant is enforced in exactly one place.
//   * `next()` / `prev()` wrap with modular arithmetic; when no widget is
//     currently focused, next() lands on chain[0] and prev() lands on the
//     last element -- matches the expected Tab behaviour from an empty
//     focus state.
// =============================================================================
#include <cd/ui/input/Input.hpp>

#include <cstddef>
#include <limits>

namespace cd::ui::input
{

// ---- HitTester -------------------------------------------------------------

WidgetId HitTester::hit_test(std::span<const HitRect> rects,
                             float x,
                             float y) noexcept
{
    // Walk back-to-front so the topmost (last-drawn) rect wins on overlap.
    for (std::size_t i = rects.size(); i > 0U; --i)
    {
        const HitRect& r = rects[i - 1U];
        if (!r.id.is_valid())
        {
            continue;
        }
        if (r.width <= 0.0F || r.height <= 0.0F)
        {
            continue;
        }
        const float x1 = r.x + r.width;
        const float y1 = r.y + r.height;
        if (x >= r.x && x < x1 && y >= r.y && y < y1)
        {
            return r.id;
        }
    }
    return kInvalidWidget;
}

// ---- FocusManager: chain mutation -----------------------------------------

void FocusManager::register_widget(WidgetId id)
{
    if (!id.is_valid())
    {
        return;
    }
    chain_.push_back(id);
}

void FocusManager::set_chain(std::span<const WidgetId> chain)
{
    chain_.assign(chain.begin(), chain.end());

    // Drop focus if the previously-focused widget is no longer reachable.
    if (focused_.is_valid() && !is_in_active_chain(focused_))
    {
        focused_ = kInvalidWidget;
    }
}

void FocusManager::clear() noexcept
{
    chain_.clear();
    modal_stack_.clear();
    focused_ = kInvalidWidget;
}

// ---- FocusManager: focus state --------------------------------------------

bool FocusManager::focus(WidgetId id) noexcept
{
    if (!id.is_valid())
    {
        return false;
    }
    if (!is_in_active_chain(id))
    {
        return false;
    }
    if (focused_ == id)
    {
        return false;
    }
    focused_ = id;
    return true;
}

void FocusManager::blur() noexcept
{
    focused_ = kInvalidWidget;
}

WidgetId FocusManager::next() noexcept
{
    const auto chain = active_chain_();
    if (chain.empty())
    {
        focused_ = kInvalidWidget;
        return kInvalidWidget;
    }

    if (!focused_.is_valid())
    {
        focused_ = chain.front();
        return focused_;
    }

    const std::size_t idx = index_of_(focused_);
    if (idx == std::numeric_limits<std::size_t>::max())
    {
        focused_ = chain.front();
        return focused_;
    }

    const std::size_t nxt = (idx + 1U) % chain.size();
    focused_ = chain[nxt];
    return focused_;
}

WidgetId FocusManager::prev() noexcept
{
    const auto chain = active_chain_();
    if (chain.empty())
    {
        focused_ = kInvalidWidget;
        return kInvalidWidget;
    }

    if (!focused_.is_valid())
    {
        focused_ = chain.back();
        return focused_;
    }

    const std::size_t idx = index_of_(focused_);
    if (idx == std::numeric_limits<std::size_t>::max())
    {
        focused_ = chain.back();
        return focused_;
    }

    const std::size_t prv = (idx == 0U) ? (chain.size() - 1U) : (idx - 1U);
    focused_ = chain[prv];
    return focused_;
}

// ---- FocusManager: modal capture ------------------------------------------

std::size_t FocusManager::push_modal(WidgetId id)
{
    ModalFrame frame;
    frame.modal_id      = id;
    frame.prior_focused = focused_;
    modal_stack_.push_back(std::move(frame));

    // On push, focus falls back to the modal id (the most natural
    // "first focusable in modal" candidate). Frontend can override by
    // calling focus(...) after push.
    if (id.is_valid())
    {
        focused_ = id;
    }
    else
    {
        focused_ = kInvalidWidget;
    }
    return modal_stack_.size();
}

void FocusManager::register_modal_subchain(std::span<const WidgetId> sub_chain)
{
    if (modal_stack_.empty())
    {
        return;
    }
    auto& frame = modal_stack_.back();
    frame.sub_chain.assign(sub_chain.begin(), sub_chain.end());

    // If the current focus is no longer reachable inside the new sub-chain,
    // pull it back to the modal id (or the first sub-chain entry).
    if (!is_in_active_chain(focused_))
    {
        if (!frame.sub_chain.empty())
        {
            focused_ = frame.sub_chain.front();
        }
        else
        {
            focused_ = frame.modal_id;
        }
    }
}

std::size_t FocusManager::pop_modal() noexcept
{
    if (modal_stack_.empty())
    {
        return 0U;
    }
    const WidgetId prior = modal_stack_.back().prior_focused;
    modal_stack_.pop_back();

    // Restore the prior focus -- but only if it's still in the (now
    // active) chain. Otherwise drop to kInvalidWidget.
    if (prior.is_valid() && is_in_active_chain(prior))
    {
        focused_ = prior;
    }
    else
    {
        focused_ = kInvalidWidget;
    }
    return modal_stack_.size();
}

bool FocusManager::is_in_active_chain(WidgetId id) const noexcept
{
    if (!id.is_valid())
    {
        return false;
    }
    const auto chain = active_chain_();
    for (const WidgetId entry : chain)
    {
        if (entry == id)
        {
            return true;
        }
    }
    return false;
}

// ---- FocusManager: private helpers ----------------------------------------

std::span<const WidgetId> FocusManager::active_chain_() const noexcept
{
    if (modal_stack_.empty())
    {
        return std::span<const WidgetId>(chain_.data(), chain_.size());
    }
    const auto& top = modal_stack_.back();
    if (!top.sub_chain.empty())
    {
        return std::span<const WidgetId>(top.sub_chain.data(), top.sub_chain.size());
    }
    // No sub-chain registered: the modal id is the entire active chain.
    return std::span<const WidgetId>(&top.modal_id, 1U);
}

std::size_t FocusManager::index_of_(WidgetId id) const noexcept
{
    const auto chain = active_chain_();
    for (std::size_t i = 0U; i < chain.size(); ++i)
    {
        if (chain[i] == id)
        {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

}  // namespace cd::ui::input
