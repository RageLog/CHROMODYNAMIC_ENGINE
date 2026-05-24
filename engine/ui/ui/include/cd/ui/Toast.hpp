// =============================================================================
// CHROMODYNAMIC — cd/ui/Toast.hpp
// Phase 67.B / Wave 235 — timed notification queue ("toasts").
//
// Editor surfaces transient notifications: "Scene saved", "Compile
// failed: …", "Texture imported". ToastQueue stores ID + message +
// expiry timestamp; `update(now)` evicts expired entries; `active()`
// returns the current visible set.
//
// Caller drives the monotonic clock — keeps the queue testable.
// Severity is a soft hint for renderer styling (info / warn / error).
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

struct Toast
{
    std::uint64_t id { 0 };
    std::string   message;
    ToastSeverity severity { ToastSeverity::kInfo };
    double        expiry_t { 0.0 };
};

class ToastQueue
{
public:
    std::uint64_t push(std::string message, double duration_s, double now,
                       ToastSeverity sev = ToastSeverity::kInfo)
    {
        const std::uint64_t id = ++next_id_;
        toasts_.push_back(Toast { id, std::move(message), sev, now + duration_s });
        return id;
    }

    /// Drop toasts whose expiry has passed. Call once per frame.
    void update(double now)
    {
        toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
            [now](const Toast& t) { return t.expiry_t <= now; }),
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
