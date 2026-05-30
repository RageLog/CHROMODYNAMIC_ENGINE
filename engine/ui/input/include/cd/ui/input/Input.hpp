// =============================================================================
// CHROMODYNAMIC — cd/ui/input/Input.hpp
//
// Phase 2.1 of ADR-20260530-ui-widget-library. Pure-C++ input plumbing for
// the retained-mode UI: hit-test walk, focus chain, tab/shift-tab cycling,
// and modal capture. No RHI, no font, no platform window dep -- the
// frontend (editor / app / cd::events bridge) translates raw input into
// MouseEvent / KeyEvent and feeds them in.
//
// Phase 2.1 scope (this header):
//   * MouseEvent / KeyEvent POD wire types (pointer + keyboard surface;
//     gesture / gamepad / text-input deferred to Phase 2.2).
//   * HitTester  -- linear walk over a caller-supplied (id, rect) list.
//                   Topmost match wins (last in z-order); kInvalidWidget on
//                   miss. The list is supplied per query so callers can
//                   reuse the same tester across multiple widget trees.
//   * FocusManager -- stable focus chain (push_back order = tab order),
//                     focus/blur/next/prev. Modal capture stack: while
//                     a modal id is on top of the stack, focus_next/prev
//                     are restricted to that modal's sub-chain (or to the
//                     modal alone when no sub-chain is registered),
//                     blocking the underneath widgets per ADR.
//
// Out of Phase 2.1 (Phase 2.2+):
//   * Drag / double-click / long-press gesture recognizers.
//   * Gamepad focus navigation (D-pad / left-stick mapping).
//   * Text-input compose / IME.
//   * Bridge to cd::events (this lib stays input-source-agnostic).
//
// Design notes:
//   * WidgetId is an opaque 32-bit handle owned by the cd::ui widget tree.
//     This library does NOT own widget storage -- it just routes ids.
//     kInvalidWidget = 0xFFFFFFFFu (matches sentinel convention used by
//     cd::ui::layout::NodeId).
//   * HitTester is intentionally stateless: pure function over a rect
//     list. Frame-to-frame churn is the caller's problem (re-emit the
//     list each frame; cheap because rects are 5 floats each).
//   * FocusManager owns the chain + modal stack but not the widget tree;
//     register(id) appends to the chain, set_chain(span) replaces it
//     wholesale for callers who rebuild every frame.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace cd::ui::input
{

// ---- WidgetId + sentinel ---------------------------------------------------

/// Opaque widget identifier. Stable for the lifetime of the cd::ui widget
/// tree that produced it; meaningful only within the producer's scope.
struct WidgetId
{
    std::uint32_t value { 0xFFFFFFFFu };

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return value != 0xFFFFFFFFu;
    }

    [[nodiscard]] constexpr bool operator==(WidgetId other) const noexcept
    {
        return value == other.value;
    }

    [[nodiscard]] constexpr bool operator!=(WidgetId other) const noexcept
    {
        return value != other.value;
    }
};

inline constexpr WidgetId kInvalidWidget { 0xFFFFFFFFu };

// ---- Event POD wire types --------------------------------------------------

/// Mouse button id. Names match the de-facto SDL / GLFW convention so the
/// frontend bridge has zero translation overhead.
enum class MouseButton : std::uint8_t
{
    kLeft   = 0,
    kRight  = 1,
    kMiddle = 2,
    kX1     = 3,
    kX2     = 4,
};

/// Mouse action. Move events carry no button (button = kLeft is fine and
/// ignored). Scroll wheel is out of Phase 2.1 -- emit a fresh action later.
enum class MouseAction : std::uint8_t
{
    kMove    = 0,
    kPress   = 1,
    kRelease = 2,
};

/// One frame's worth of pointer state. Coordinates are framebuffer-pixel
/// space (top-left origin) -- same space as the rects in HitTester.
struct MouseEvent
{
    float       x       { 0.0F };
    float       y       { 0.0F };
    MouseButton button  { MouseButton::kLeft };
    MouseAction action  { MouseAction::kMove };
};

/// Bit-set modifier flags. Matches the GLFW bitmask layout so the frontend
/// bridge can `static_cast` from GLFW_MOD_*. Repeat / num-lock / caps-lock
/// are out of Phase 2.1.
namespace key_mods
{
inline constexpr std::uint16_t kShift   = 1U << 0;
inline constexpr std::uint16_t kControl = 1U << 1;
inline constexpr std::uint16_t kAlt     = 1U << 2;
inline constexpr std::uint16_t kSuper   = 1U << 3;
}  // namespace key_mods

/// Key action.
enum class KeyAction : std::uint8_t
{
    kPress   = 0,
    kRelease = 1,
    kRepeat  = 2,
};

/// One key event. `keycode` is the raw platform scancode-equivalent; the
/// widget layer maps it to semantic actions (Tab, Enter, Escape, ...).
struct KeyEvent
{
    std::uint32_t keycode    { 0U };
    KeyAction     action     { KeyAction::kPress };
    std::uint16_t modifiers  { 0U };
};

// ---- Hit-test rect ---------------------------------------------------------

/// One (id, rect) entry in the hit-test list. The caller supplies the list
/// in z-order (back to front); HitTester walks it back-to-front so the
/// LAST matching entry wins -- the topmost rect under the cursor.
struct HitRect
{
    WidgetId id     { kInvalidWidget };
    float    x      { 0.0F };
    float    y      { 0.0F };
    float    width  { 0.0F };
    float    height { 0.0F };
};

/// Stateless hit-test helper. The free-function form is enough for callers
/// who don't want to keep a tester object alive; the struct form is here
/// for symmetry with FocusManager and to allow future amortised data
/// (R-tree / quad-tree) without changing the call sites.
struct HitTester
{
    /// Return the topmost rect under `(x, y)`, or kInvalidWidget on a miss.
    /// Walk order: caller-provided order treated as z-order, last-wins on
    /// overlap. O(N) per query.
    [[nodiscard]] static WidgetId hit_test(std::span<const HitRect> rects,
                                           float x,
                                           float y) noexcept;
};

// ---- Focus chain + manager -------------------------------------------------

/// Read-only view of a focus chain. Slice exposed by FocusManager so the
/// renderer can draw a focus indicator without copying the vector.
using FocusChain = std::span<const WidgetId>;

/// Owns the focus chain + modal capture stack. NOT thread-safe; the cd::ui
/// dispatcher is single-threaded by ADR contract (one input dispatch per
/// frame on the UI thread).
class FocusManager
{
public:
    FocusManager() = default;
    ~FocusManager() = default;

    FocusManager(const FocusManager&) = delete;
    FocusManager& operator=(const FocusManager&) = delete;
    FocusManager(FocusManager&&) noexcept = default;
    FocusManager& operator=(FocusManager&&) noexcept = default;

    // ---- Chain mutation ----------------------------------------------------

    /// Append `id` to the focus chain. Order = tab order. Caller-side
    /// dedupe responsibility (a widget id should only appear once).
    void register_widget(WidgetId id);

    /// Replace the entire chain. Useful for callers who rebuild every
    /// frame (immediate-mode shim) -- avoids the per-widget register call.
    /// Resets `focused()` to kInvalidWidget if the previously focused id
    /// is no longer present.
    void set_chain(std::span<const WidgetId> chain);

    /// Clear the chain and the modal stack. Focus becomes kInvalidWidget.
    void clear() noexcept;

    /// Number of widgets in the focus chain.
    [[nodiscard]] std::size_t chain_size() const noexcept { return chain_.size(); }

    /// Read-only view of the focus chain (tab order).
    [[nodiscard]] FocusChain focus_chain() const noexcept
    {
        return FocusChain(chain_.data(), chain_.size());
    }

    // ---- Focus state -------------------------------------------------------

    /// Currently-focused widget, or kInvalidWidget when no widget has focus.
    [[nodiscard]] WidgetId focused() const noexcept { return focused_; }

    /// Set focus to `id`. The id must be in the active chain (the full
    /// chain when no modal is active; the modal sub-chain otherwise) --
    /// otherwise this is a silent no-op and `focused()` is unchanged.
    /// Returns true when focus actually changed.
    bool focus(WidgetId id) noexcept;

    /// Clear focus (kInvalidWidget). Does NOT pop the modal stack.
    void blur() noexcept;

    /// Advance focus to the next widget in the active chain (tab). Wraps
    /// at the end. No-op when the active chain is empty. Returns the
    /// newly-focused widget id (kInvalidWidget when nothing focusable).
    WidgetId next() noexcept;

    /// Reverse of next() (shift-tab). Wraps at the start.
    WidgetId prev() noexcept;

    // ---- Modal capture -----------------------------------------------------

    /// Push `id` onto the modal stack. While this id is on top, focus
    /// queries / cycling are restricted to its sub-chain (set via
    /// register_modal_subchain) or, when no sub-chain is registered, to
    /// the modal id alone. Focus underneath is preserved (and restored
    /// on pop_modal).
    ///
    /// Behaviour summary (per ADR Section 2.4):
    ///   * `hit_test` is the caller's responsibility -- the modal-aware
    ///     hit-test happens in the frontend by clipping the rect list to
    ///     the modal sub-tree before calling HitTester::hit_test.
    ///   * Tab navigation is restricted to the modal sub-chain here.
    ///
    /// Returns the new stack depth (>=1).
    std::size_t push_modal(WidgetId id);

    /// Sub-chain for the currently active modal. Replaces the previous
    /// sub-chain. No-op when the modal stack is empty.
    void register_modal_subchain(std::span<const WidgetId> sub_chain);

    /// Pop the top modal. Restores the prior focus state (the widget that
    /// was focused before push_modal was called). Returns the new stack
    /// depth (0 when fully unwound).
    std::size_t pop_modal() noexcept;

    /// Current modal stack depth (0 == no modal active).
    [[nodiscard]] std::size_t modal_depth() const noexcept { return modal_stack_.size(); }

    /// Widget id currently capturing modal input, or kInvalidWidget when
    /// no modal is active.
    [[nodiscard]] WidgetId modal_top() const noexcept
    {
        return modal_stack_.empty() ? kInvalidWidget
                                    : modal_stack_.back().modal_id;
    }

    /// True when `id` is reachable from focus-cycling in the current
    /// modal context. Exposed for the frontend dispatcher so it can drop
    /// pointer events whose target is below the active modal.
    [[nodiscard]] bool is_in_active_chain(WidgetId id) const noexcept;

private:
    struct ModalFrame
    {
        WidgetId               modal_id;
        std::vector<WidgetId>  sub_chain;     ///< empty == "modal id only"
        WidgetId               prior_focused; ///< restored on pop
    };

    std::vector<WidgetId>   chain_;
    std::vector<ModalFrame> modal_stack_;
    WidgetId                focused_ { kInvalidWidget };

    /// Active chain = top modal sub-chain (or the modal id alone) when a
    /// modal is on the stack; otherwise the full chain_.
    [[nodiscard]] std::span<const WidgetId> active_chain_() const noexcept;

    /// Linear search for `id` in the active chain. Returns chain index
    /// or SIZE_MAX on miss.
    [[nodiscard]] std::size_t index_of_(WidgetId id) const noexcept;
};

}  // namespace cd::ui::input
