// =============================================================================
// CHROMODYNAMIC — cd/ui/input/Input.cpp
//
// Phase 2.1 + Phase 2.2 implementation. See Input.hpp for the scope + API
// contract.
//
// Algorithm notes (Phase 2.1):
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
//
// Algorithm notes (Phase 2.2 — GestureRecognizer):
//   * One state machine per recognizer instance; multiple instances on the
//     same stream behave independently.
//   * Press: record press position + timestamp; arm long-press window.
//   * Move (while pressed): if displacement > drag_threshold_px start drag.
//     Starting drag cancels the long-press (no double-gesture).
//   * Release:
//       - If drag was active: nothing extra (drag already fired on first move
//         past threshold).
//       - Else if elapsed >= long_press_window_ms and drag not active: emit
//         kLongPress (only if not already fired from move path).
//       - Else: check double-click window against last_click_t_; emit
//         kDoubleClick and invalidate last_click or record click for next.
//   * Two-pointer pinch: tracked via pointer_[0] and pointer_[1] slots.
//     When both slots are active and the inter-pointer distance changes by
//     more than drag_threshold_px from the pinch-start distance the
//     appropriate kPinchIn / kPinchOut is emitted.
//   * Swipe: emitted on release when drag was active and velocity (distance /
//     duration) exceeds swipe_min_velocity_px_per_s. Direction is determined
//     by the dominant axis of the displacement vector.
// =============================================================================
#include <cd/ui/input/Input.hpp>

#include <cmath>
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

// ---- GestureRecognizer -----------------------------------------------------

GestureRecognizer::GestureRecognizer(GestureThresholds thresholds) noexcept
    : thresholds_(thresholds)
{
}

void GestureRecognizer::on_gesture(Callback cb)
{
    callback_ = std::move(cb);
}

void GestureRecognizer::reset() noexcept
{
    pressed_          = false;
    press_x_          = 0.0F;
    press_y_          = 0.0F;
    cur_x_            = 0.0F;
    cur_y_            = 0.0F;
    t_press_          = 0.0;
    drag_active_      = false;
    long_press_fired_ = false;
    last_click_valid_ = false;
    last_click_x_     = 0.0F;
    last_click_y_     = 0.0F;
    last_click_t_     = 0.0;
    pointer_[0]       = PointerSlot{};
    pointer_[1]       = PointerSlot{};
    pinch_start_dist_ = 0.0F;
}

void GestureRecognizer::feed(const MouseEvent& ev, double t_seconds)
{
    // Route to the appropriate single-pointer handler first.
    switch (ev.action)
    {
    case MouseAction::kPress:
        handle_press_(ev, t_seconds);
        break;
    case MouseAction::kRelease:
        handle_release_(ev, t_seconds);
        break;
    case MouseAction::kMove:
        handle_move_(ev, t_seconds);
        break;
    default:
        break;
    }

    // Two-pointer (pinch) tracking: map left/right/middle buttons to slots.
    // Convention: kLeft -> slot 0, kRight -> slot 1 (synthetic two-pointer).
    const int slot_idx = (ev.button == MouseButton::kRight) ? 1 : 0;
    auto& slot = pointer_[slot_idx];

    if (ev.action == MouseAction::kPress)
    {
        slot.active    = true;
        slot.x         = ev.x;
        slot.y         = ev.y;
        slot.press_x   = ev.x;
        slot.press_y   = ev.y;
        slot.t_press_s = static_cast<float>(t_seconds);

        // Both slots became active -> record initial pinch distance.
        if (pointer_[0].active && pointer_[1].active)
        {
            pinch_start_dist_ = dist_(pointer_[0].x, pointer_[0].y,
                                      pointer_[1].x, pointer_[1].y);
        }
    }
    else if (ev.action == MouseAction::kRelease)
    {
        slot.active = false;
        pinch_start_dist_ = 0.0F;
    }
    else if (ev.action == MouseAction::kMove)
    {
        slot.x = ev.x;
        slot.y = ev.y;

        // Evaluate pinch while both pointers are active.
        if (pointer_[0].active && pointer_[1].active &&
            pinch_start_dist_ > 0.0F)
        {
            const float cur_dist = dist_(pointer_[0].x, pointer_[0].y,
                                         pointer_[1].x, pointer_[1].y);
            const float diff = cur_dist - pinch_start_dist_;

            if (diff > thresholds_.drag_threshold_px)
            {
                const float scale =
                    (pinch_start_dist_ > 0.0F) ? (cur_dist / pinch_start_dist_)
                                                : 1.0F;
                const Vec2 centre {
                    (pointer_[0].x + pointer_[1].x) * 0.5F,
                    (pointer_[0].y + pointer_[1].y) * 0.5F,
                };
                emit_(GestureKind::kPinchOut, centre, {}, scale);
                pinch_start_dist_ = cur_dist;  // consume delta
            }
            else if (diff < -thresholds_.drag_threshold_px)
            {
                const float scale =
                    (pinch_start_dist_ > 0.0F) ? (cur_dist / pinch_start_dist_)
                                                : 1.0F;
                const Vec2 centre {
                    (pointer_[0].x + pointer_[1].x) * 0.5F,
                    (pointer_[0].y + pointer_[1].y) * 0.5F,
                };
                emit_(GestureKind::kPinchIn, centre, {}, scale);
                pinch_start_dist_ = cur_dist;
            }
        }
    }
}

void GestureRecognizer::feed(const KeyEvent& /*ev*/, double /*t_seconds*/)
{
    // Reserved for future keyboard gesture detection (e.g. arrow-key swipe).
    // Currently a no-op; callers may unconditionally forward all events.
}

// ---- private helpers -------------------------------------------------------

void GestureRecognizer::emit_(GestureKind kind,
                               Vec2        pos,
                               Vec2        delta,
                               float       scale)
{
    if (callback_)
    {
        callback_(GestureEvent { kind, pos, delta, scale });
    }
}

void GestureRecognizer::handle_press_(const MouseEvent& ev, double t)
{
    // Ignore secondary button for single-pointer state (handled in pinch path).
    if (ev.button != MouseButton::kLeft)
    {
        return;
    }
    pressed_          = true;
    press_x_          = ev.x;
    press_y_          = ev.y;
    cur_x_            = ev.x;
    cur_y_            = ev.y;
    t_press_          = t;
    drag_active_      = false;
    long_press_fired_ = false;
}

void GestureRecognizer::handle_release_(const MouseEvent& ev, double t)
{
    if (ev.button != MouseButton::kLeft)
    {
        return;
    }
    if (!pressed_)
    {
        return;
    }
    pressed_ = false;

    const Vec2 pos { ev.x, ev.y };

    if (drag_active_)
    {
        // Check for swipe: velocity = distance / duration.
        const float dx       = ev.x - press_x_;
        const float dy       = ev.y - press_y_;
        const float dist_val = std::sqrt(dx * dx + dy * dy);
        const double elapsed = t - t_press_;
        if (elapsed > 0.0 && dist_val > 0.0F)
        {
            const float velocity = dist_val / static_cast<float>(elapsed);
            if (velocity >= thresholds_.swipe_min_velocity_px_per_s)
            {
                const Vec2 delta { dx, dy };
                emit_(swipe_direction_(delta), pos, delta);
            }
        }
        drag_active_ = false;
        return;
    }

    // Not a drag; check long-press (only fire here if not already fired).
    if (!long_press_fired_)
    {
        const auto elapsed_ms = static_cast<float>((t - t_press_) * 1000.0);
        if (elapsed_ms >= thresholds_.long_press_window_ms)
        {
            emit_(GestureKind::kLongPress, pos);
            last_click_valid_ = false;
            return;
        }
    }

    // Regular click -> check double-click.
    if (last_click_valid_)
    {
        const auto window_ms =
            static_cast<float>((t - last_click_t_) * 1000.0);
        if (window_ms <= thresholds_.double_click_window_ms)
        {
            emit_(GestureKind::kDoubleClick, pos);
            last_click_valid_ = false;  // consume -- next click is fresh
            return;
        }
    }

    // Record this click for the next release.
    last_click_valid_ = true;
    last_click_x_     = ev.x;
    last_click_y_     = ev.y;
    last_click_t_     = t;
}

void GestureRecognizer::handle_move_(const MouseEvent& ev, double /*t*/)
{
    cur_x_ = ev.x;
    cur_y_ = ev.y;

    if (!pressed_ || drag_active_)
    {
        return;
    }

    const float dx = ev.x - press_x_;
    const float dy = ev.y - press_y_;
    const float d  = std::sqrt(dx * dx + dy * dy);

    if (d >= thresholds_.drag_threshold_px)
    {
        drag_active_      = true;
        long_press_fired_ = true;  // suppress long-press when dragging
        const Vec2 pos { ev.x, ev.y };
        const Vec2 delta { dx, dy };
        emit_(GestureKind::kDrag, pos, delta);
    }
}

float GestureRecognizer::dist_(float ax, float ay, float bx, float by) const noexcept
{
    const float dx = bx - ax;
    const float dy = by - ay;
    return std::sqrt(dx * dx + dy * dy);
}

GestureKind GestureRecognizer::swipe_direction_(Vec2 delta) noexcept
{
    const float ax = std::abs(delta.x);
    const float ay = std::abs(delta.y);

    if (ax >= ay)
    {
        return (delta.x >= 0.0F) ? GestureKind::kSwipeRight
                                  : GestureKind::kSwipeLeft;
    }
    return (delta.y >= 0.0F) ? GestureKind::kSwipeDown
                              : GestureKind::kSwipeUp;
}

}  // namespace cd::ui::input
