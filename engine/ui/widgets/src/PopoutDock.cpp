// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/PopoutDock.cpp
//
// Phase 689 — PopoutDock implementation.
//
// Sprint-1 scope: pure in-process state tracking.
// Sprint-2 scope (future): native OS-window creation/destruction via
//   cd::platform::MultiWindowContext.
// =============================================================================
#include <cd/ui/widgets/PopoutDock.hpp>

#include <algorithm>
#include <utility>

namespace cd::ui::widgets
{

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

PopoutWindow* PopoutDock::find_(std::string_view panel_id) noexcept
{
    auto it = std::ranges::find_if(windows_,
        [panel_id](const PopoutWindow& w) { return w.panel_id == panel_id; });
    return (it != windows_.end()) ? &(*it) : nullptr;
}

const PopoutWindow* PopoutDock::find_(std::string_view panel_id) const noexcept
{
    // const/non-const dedup; the non-const overload only searches.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<PopoutDock*>(this)->find_(panel_id);
}

// ---------------------------------------------------------------------------
// Detach / reattach
// ---------------------------------------------------------------------------

bool PopoutDock::detach_panel(std::string_view panel_id,
                               std::array<float, 2U> position,
                               std::array<float, 2U> size)
{
    if (find_(panel_id) != nullptr)
    {
        // Already detached — idempotent guard.
        return false;
    }
    windows_.push_back(PopoutWindow {
        .panel_id  = std::string { panel_id },
        .position  = position,
        .size      = size,
        .is_active = true,
    });
    return true;
}

bool PopoutDock::reattach_panel(std::string_view panel_id)
{
    const auto it = std::ranges::find_if(windows_,
        [panel_id](const PopoutWindow& w) { return w.panel_id == panel_id; });
    if (it == windows_.end()) { return false; }
    windows_.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool PopoutDock::is_detached(std::string_view panel_id) const noexcept
{
    return find_(panel_id) != nullptr;
}

std::span<const PopoutWindow> PopoutDock::detached_windows() const noexcept
{
    return {windows_.data(), windows_.size()};
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void PopoutDock::simulate_drag(std::string_view panel_id,
                                std::array<float, 2U> new_position) noexcept
{
    PopoutWindow* w = find_(panel_id);
    if (w != nullptr)
    {
        w->position = new_position;
    }
}

}  // namespace cd::ui::widgets
