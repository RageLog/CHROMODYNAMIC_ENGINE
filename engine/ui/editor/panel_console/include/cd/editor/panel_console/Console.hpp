// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_console/Console.hpp
//
// phase544 — cd::editor::panel::console  (panel_console library)
//
// Console panel: shows the last N editor log messages in reverse-chronological
// order (newest at the top). Provides two draw paths:
//
//   * draw()        — DrawBatcher-based path for the DockSpace shell
//                     (apps/editor). Renders a coloured background with one
//                     dimmed row per visible log entry.
//
// State API:
//   push_log(string_view) — append a message (oldest entries are evicted when
//                           the ring is full; capacity = 32).
//   clear()               — remove all stored entries.
//   entry_count() const   — number of entries currently stored (0..32).
//
// Lifetime contract:
//   Console is default-constructible and owns its log storage. No external
//   pointer ownership is required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace cd::editor::panel::console
{

class Console
{
public:
    /// Maximum number of log entries retained.
    static constexpr std::size_t kMaxEntries = 32U;

    // Default-constructible; starts with an empty log.
    Console() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Append `msg` to the log. If the log is at capacity the oldest entry is
    /// discarded to make room.
    void push_log(std::string_view msg);

    /// Remove all stored log entries.
    void clear() noexcept;

    /// Returns the number of entries currently stored (0 .. kMaxEntries).
    [[nodiscard]] std::size_t entry_count() const noexcept;

    /// Read-only view of the stored entries (oldest first).
    /// Invalidated by any call to push_log() or clear().
    [[nodiscard]] const std::deque<std::string>& entries() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders the console panel background + one row per log entry (newest
    /// first) using the DrawBatcher solid-quad API.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    std::deque<std::string> entries_;   ///< Stored log messages (front = oldest).
};

}  // namespace cd::editor::panel::console
