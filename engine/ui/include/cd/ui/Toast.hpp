// =============================================================================
// CHROMODYNAMIC -- cd/ui/Toast.hpp
// Phase 67.B / Wave 235 -- timed notification queue ("toasts").
// phase774 -- Direction enum + multi-toast vertical stacking.
//
// Editor surfaces transient notifications: "Scene saved", "Compile
// failed: ...", "Texture imported". ToastQueue stores ID + message +
// expiry timestamp; `update(now)` evicts expired entries; `active()`
// returns the current visible set.
//
// Caller drives the monotonic clock -- keeps the queue testable.
// Severity is a soft hint for renderer styling (info / warn / error).
//
// Direction controls which screen edge the toast slides in from.
// Multi-stack: when N toasts are active they are laid out vertically
// with a 4 px gap; callers use toast_stack_y_offset() to compute the
// per-toast Y position.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace cd::ui
{

enum class ToastSeverity : std::uint8_t
{
    kInfo  = 0,
    kWarn  = 1,
    kError = 2,
};

/// Which screen edge the toast slides in from.
/// Default is kFromRight (backward-compatible with phase724).
enum class ToastDirection : std::uint8_t
{
    kFromRight  = 0,  ///< Slides in from the right edge (default).
    kFromLeft   = 1,  ///< Slides in from the left edge.
    kFromTop    = 2,  ///< Slides in from the top edge.
    kFromBottom = 3,  ///< Slides in from the bottom edge.
};

struct Toast
{
    std::uint64_t  id        { 0 };
    std::string    message;
    ToastSeverity  severity  { ToastSeverity::kInfo };
    ToastDirection direction { ToastDirection::kFromRight };
    double         expiry_t  { 0.0 };
};

/// Compute the Y-axis pixel offset for toast at slot `index` in a vertical
/// stack.  Toasts accumulate upward from the anchor edge: index 0 is the
/// bottom-most, index N-1 is topmost.
///
/// @param index       Zero-based slot in the active stack (0 = newest / lowest).
/// @param toast_h_px  Height of a single toast widget in pixels.
/// @param gap_px      Gap between consecutive toasts (default 4 px).
/// @return            Y offset (in pixels, positive = upward from anchor).
[[nodiscard]] inline float
toast_stack_y_offset(std::size_t index,
                     float       toast_h_px,
                     float       gap_px = 4.0F) noexcept
{
    return static_cast<float>(index) * (toast_h_px + gap_px);
}

class ToastQueue
{
public:
    /// Push a new toast.  direction defaults to kFromRight (phase724 compat).
    std::uint64_t push(std::string    message,
                       double         duration_s,
                       double         now,
                       ToastSeverity  sev = ToastSeverity::kInfo,
                       ToastDirection dir = ToastDirection::kFromRight)
    {
        const std::uint64_t id = ++next_id_;
        toasts_.push_back(Toast { id, std::move(message), sev, dir, now + duration_s });
        return id;
    }

    /// Convenience alias matching the spec (add_toast).
    std::uint64_t add_toast(std::string    message,
                            double         duration_s,
                            double         now,
                            ToastSeverity  sev = ToastSeverity::kInfo,
                            ToastDirection dir = ToastDirection::kFromRight)
    {
        return push(std::move(message), duration_s, now, sev, dir);
    }

    /// Drop toasts whose expiry has passed. Call once per frame.
    void update(double now)
    {
        toasts_.erase(std::ranges::remove_if(toasts_,
            [now](const Toast& t) { return t.expiry_t <= now; }).begin(),
            toasts_.end());
    }

    [[nodiscard]] const std::vector<Toast>& active() const noexcept { return toasts_; }

    [[nodiscard]] std::size_t size() const noexcept { return toasts_.size(); }

    bool dismiss(std::uint64_t id)
    {
        for (auto it = toasts_.begin(); it != toasts_.end(); ++it)
        {
            if (it->id == id) { toasts_.erase(it); return true; }
        }
        return false;
    }

    void clear() noexcept { toasts_.clear(); }

private:
    std::vector<Toast> toasts_;
    std::uint64_t      next_id_ { 0 };
};

}  // namespace cd::ui