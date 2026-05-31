// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/CurveEditor.hpp
//
// Phase 5.17 of ADR-20260530-ui-widget-library. Bezier curve editor widget
// with multi-curve overlay support, viewport pan/zoom, click-to-add, drag
// handles, and right-click tangent mode menu.
//
// Dependencies:
//   * cd::ui_widgets    -- Rect / Color / Theme / PointerState / InputState
//   * cd::ui_renderer   -- DrawBatcher (draw path only)
//   * cd::ui_animation  -- Tweener / Easing (pluggable via EasingCurveSource)
//
// Public type hierarchy:
//
//   KeyFrame           -- {time, value, in_tangent, out_tangent, TangentMode}
//   Curve              -- std::vector<KeyFrame> + evaluate(t) -> float
//   CurveEditor        -- multi-curve overlay, pan/zoom, pointer-driven editing
//
// Eval API contract:
//   Curve::evaluate(t) is directly usable as a custom easing source for
//   cd::ui::animation::Tweener<float>. Supply a lambda/functor that wraps
//   a Curve instance; the Tweener receives a normalised [0..1] t parameter.
//
// TangentMode semantics:
//   kAuto    -- Catmull-Rom-like automatic smooth tangents (re-computed on
//               keyframe add/move). Tangent vectors match the standard
//               Catmull-Rom formula: slope = 0.5*(p[i+1] - p[i-1]).
//   kLinear  -- Tangents directed straight toward adjacent keyframes,
//               producing linear segments.
//   kStepped -- out_tangent forced to zero; holds previous value until the
//               next keyframe time is reached (sample returns prev value).
//   kFree    -- User-dragged tangent handles; no automatic recomputation.
//
// Coordinate spaces:
//   "curve space" -- (time, value) as stored in KeyFrame.
//   "widget space" -- pixel coords of the CurveEditor Rect.
//   Viewport pan (pan_x_, pan_y_) and zoom (zoom_x_, zoom_y_) map between
//   the two via curve_to_widget / widget_to_curve helpers defined in the .cpp.
//
// Thread safety: none -- all mutation on the same thread (UI thread).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cd::ui::widgets
{

// ---- TangentMode -----------------------------------------------------------

/// Controls how the in/out tangent handles are computed and constrained.
enum class TangentMode : std::uint8_t
{
    kAuto    = 0,  ///< Catmull-Rom automatic smooth tangents
    kLinear  = 1,  ///< Directed toward adjacent keyframe (linear segment)
    kStepped = 2,  ///< Hold previous value; step at keyframe time
    kFree    = 3,  ///< User-defined; no automatic recomputation
};

// ---- KeyFrame --------------------------------------------------------------

/// One sample point on a Curve with cubic-Hermite tangent handles.
///
/// Tangent convention (matching standard animation packages):
///   in_tangent  -- slope arriving at this keyframe from the left.
///   out_tangent -- slope leaving this keyframe toward the right.
///   Both are dimensionless (dy/dt in curve-space units).
///
/// When `mode == kStepped` the segment to the right of this keyframe holds
/// `value` until the next keyframe is reached, regardless of in/out_tangent.
struct KeyFrame
{
    float       time        { 0.0F };   ///< Normalised or absolute time axis value
    float       value       { 0.0F };   ///< Curve value at this time
    float       in_tangent  { 0.0F };   ///< Arriving slope (dy/dt)
    float       out_tangent { 0.0F };   ///< Departing slope (dy/dt)
    TangentMode mode        { TangentMode::kAuto };
};

// ---- Curve -----------------------------------------------------------------

/// A single named curve: an ordered list of KeyFrames evaluated via
/// piecewise cubic-Hermite (kAuto/kFree), linear (kLinear), or stepped
/// (kStepped) interpolation.
///
/// KeyFrames are always kept sorted by `time`. Mutation methods maintain
/// this invariant; callers may set raw keyframes via `set_keyframes` which
/// also re-sorts and recomputes auto tangents.
class Curve
{
public:
    Curve() = default;
    explicit Curve(std::string name);
    explicit Curve(std::string name, std::vector<KeyFrame> keyframes);

    // ---- Identity ----------------------------------------------------------

    void set_name(std::string name) { name_ = std::move(name); }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    // ---- Color (for multi-curve overlay) -----------------------------------

    void set_color(Color c) noexcept { color_ = c; }
    [[nodiscard]] Color color() const noexcept { return color_; }

    // ---- Keyframe access ---------------------------------------------------

    [[nodiscard]] const std::vector<KeyFrame>& keyframes() const noexcept
    {
        return keyframes_;
    }

    [[nodiscard]] std::size_t keyframe_count() const noexcept
    {
        return keyframes_.size();
    }

    /// Replace entire keyframe set, re-sort by time, and recompute auto tangents.
    void set_keyframes(std::vector<KeyFrame> kfs);

    /// Add a keyframe (inserted in time order). Auto tangents are recomputed
    /// for adjacent kAuto keys. Returns the index of the inserted keyframe.
    [[nodiscard]] std::size_t add_keyframe(KeyFrame kf);

    /// Remove keyframe at `index`. Auto tangents of neighbours are recomputed.
    /// No-op if `index >= keyframe_count()`.
    void remove_keyframe(std::size_t index);

    /// Mutate a single keyframe. If the new time differs the array is re-sorted
    /// and auto tangents recomputed. Returns the (possibly new) index.
    std::size_t set_keyframe(std::size_t index, KeyFrame kf);

    // ---- Evaluation --------------------------------------------------------

    /// Evaluate the curve at time `t`.
    ///
    /// * `t < first_keyframe.time` -> returns first_keyframe.value (clamped).
    /// * `t > last_keyframe.time`  -> returns last_keyframe.value (clamped).
    /// * Between two keyframes the segment tangent mode of the LEFT keyframe
    ///   (out_tangent) and RIGHT keyframe (in_tangent) are used.
    ///
    /// Segment rules:
    ///   kStepped  -- returns left.value for all t in [left.time, right.time).
    ///   kLinear   -- linear interpolation regardless of tangent values.
    ///   kAuto/kFree -- cubic Hermite using out_tangent and in_tangent.
    [[nodiscard]] float evaluate(float t) const noexcept;

private:
    std::string          name_      {};
    Color                color_     { 96U, 160U, 255U, 255U };  ///< Default accent blue
    std::vector<KeyFrame> keyframes_ {};

    /// Recompute kAuto tangents for the full keyframe array.
    /// Uses the Catmull-Rom formula: slope_i = 0.5 * (v[i+1] - v[i-1]) / (t[i+1] - t[i-1]).
    /// Endpoints use one-sided differences.
    void recompute_auto_tangents() noexcept;

    /// Sort keyframes_ by time ascending.
    void sort_keyframes() noexcept;

    /// Find the index of the left keyframe for a given time `t` (binary search).
    /// Returns SIZE_MAX if `t` is before the first keyframe.
    [[nodiscard]] std::size_t find_segment(float t) const noexcept;
};

// ---- CurveEditor -----------------------------------------------------------

/// Multi-curve overlay editor widget.
///
/// Interaction model:
///   * Left-click on empty canvas area: add a keyframe to the "active" curve.
///   * Left-drag on a keyframe knob: move the keyframe (time + value).
///   * Left-drag on a tangent handle: drag the tangent (kFree only; auto
///     tangent knobs are not draggable -- they toggle to kFree on drag).
///   * Middle-drag (or Alt + left-drag): pan the viewport.
///   * Scroll (simulated as pointer delta): zoom along time axis.
///   * Right-click on a keyframe knob: open tangent-mode context menu.
///
/// Multi-curve overlay:
///   All Curve objects in `curves_` are drawn simultaneously. Only the
///   "active" curve (index `active_curve_`) responds to keyframe editing.
///   The active index defaults to 0 and can be changed via `set_active_curve`.
///
/// Viewport mapping:
///   curve_time in [view_t_min .. view_t_max] maps to widget [rect.x .. rect.x+rect.w].
///   curve_value in [view_v_min .. view_v_max] maps to widget [rect.y+rect.h .. rect.y]
///   (value increases upward in conventional curve-editor style).
class CurveEditor
{
public:
    using KeyFrameCallback  = std::function<void(std::size_t curve_idx,
                                                  std::size_t keyframe_idx,
                                                  const KeyFrame&)>;
    using CurveChangedCallback = std::function<void(std::size_t curve_idx)>;

    CurveEditor() = default;

    // ---- Geometry ----------------------------------------------------------

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    // ---- Curve management --------------------------------------------------

    /// Add a curve. Returns its index.
    [[nodiscard]] std::size_t add_curve(Curve c);

    /// Remove curve by index. Active index is clamped if needed.
    void remove_curve(std::size_t index);

    [[nodiscard]] std::size_t curve_count() const noexcept { return curves_.size(); }

    [[nodiscard]] const Curve& curve(std::size_t index) const { return curves_.at(index); }
    [[nodiscard]] Curve&       curve(std::size_t index)       { return curves_.at(index); }

    void set_active_curve(std::size_t index) noexcept;
    [[nodiscard]] std::size_t active_curve() const noexcept { return active_curve_; }

    // ---- Viewport ----------------------------------------------------------

    void set_view_range(float t_min, float t_max,
                        float v_min, float v_max) noexcept;

    [[nodiscard]] float view_t_min() const noexcept { return view_t_min_; }
    [[nodiscard]] float view_t_max() const noexcept { return view_t_max_; }
    [[nodiscard]] float view_v_min() const noexcept { return view_v_min_; }
    [[nodiscard]] float view_v_max() const noexcept { return view_v_max_; }

    // ---- Callbacks ---------------------------------------------------------

    void set_on_keyframe_changed(KeyFrameCallback cb) { on_kf_changed_ = std::move(cb); }
    void set_on_curve_changed(CurveChangedCallback cb) { on_curve_changed_ = std::move(cb); }

    // ---- Interaction state -------------------------------------------------

    /// Index of the currently selected keyframe (SIZE_MAX = none).
    [[nodiscard]] std::size_t selected_keyframe() const noexcept { return selected_kf_; }

    /// True when the context menu (tangent mode picker) is open.
    [[nodiscard]] bool context_menu_open() const noexcept { return ctx_menu_open_; }

    // ---- Tick / Draw -------------------------------------------------------

    /// Advance interaction state one frame. Returns true when any curve was
    /// mutated (keyframe added / moved / tangent changed).
    bool tick(const InputState& input);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font*            font,
              const Theme&                   theme) const;

private:
    // ---- Data --------------------------------------------------------------

    Rect                   rect_           {};
    std::vector<Curve>     curves_         {};
    std::size_t            active_curve_   { 0U };

    // Viewport
    float view_t_min_ { 0.0F };
    float view_t_max_ { 1.0F };
    float view_v_min_ { 0.0F };
    float view_v_max_ { 1.0F };

    // Callbacks
    KeyFrameCallback      on_kf_changed_    {};
    CurveChangedCallback  on_curve_changed_ {};

    // Interaction state
    std::size_t  selected_kf_   { static_cast<std::size_t>(-1) };
    bool         dragging_kf_   { false };
    bool         panning_       { false };
    float        pan_start_mx_  { 0.0F };
    float        pan_start_my_  { 0.0F };
    float        pan_start_t_min_ { 0.0F };
    float        pan_start_t_max_ { 0.0F };
    float        pan_start_v_min_ { 0.0F };
    float        pan_start_v_max_ { 0.0F };

    // Context menu
    bool         ctx_menu_open_ { false };
    float        ctx_menu_x_    { 0.0F };
    float        ctx_menu_y_    { 0.0F };
    std::size_t  ctx_menu_kf_   { static_cast<std::size_t>(-1) };

    // Previous pointer state (for edge detection inside tick)
    bool         prev_left_down_ { false };

    // ---- Helpers (defined in CurveEditor.cpp) ------------------------------

    /// Map curve-space (time, value) -> widget-space pixel (x, y).
    [[nodiscard]] std::pair<float, float>
    curve_to_widget(float t, float v) const noexcept;

    /// Map widget-space pixel (px, py) -> curve-space (time, value).
    [[nodiscard]] std::pair<float, float>
    widget_to_curve(float px, float py) const noexcept;

    /// Radius (pixels) used for keyframe knob hit-testing.
    static constexpr float kKnobRadius = 6.0F;

    /// Find which keyframe knob (if any) the pointer is over.
    /// Returns {curve_idx, keyframe_idx} or {SIZE_MAX, SIZE_MAX}.
    [[nodiscard]] std::pair<std::size_t, std::size_t>
    hit_test_knob(float px, float py) const noexcept;
};

}  // namespace cd::ui::widgets
