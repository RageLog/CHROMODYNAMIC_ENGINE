// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_lobby_browser/LobbyBrowser.hpp
//
// phase718 — cd::editor::panel::lobby_browser  (panel_lobby_browser library)
//
// Lobby Browser panel: DrawBatcher-based visualizer for cd::network::lobby.
// Renders the list of active rooms so a multiplayer dev can see rooms appear
// and disappear in real-time as players create/join/leave — no external
// network monitor needed.
//
// Renders (per frame):
//   * Panel background quad.
//   * Accent separator bar under the title area.
//   * One row per RoomState in the bound Lobby:
//       - Background rect (highlighted when selected).
//       - Accent-coloured left edge bar for the game-mode indicator.
//       - Room name column (represented as a wide label rect).
//       - Player-count bar (filled proportion = current / max_players).
//       - Lock indicator quad (accent_warning) when passcode is set.
//   * Empty-state placeholder when lobby is null or has no active rooms.
//
// State API:
//   set_lobby(const Lobby*)             — bind lobby (nullptr = detach).
//   selected_room_id() const            — returns selected RoomId or nullopt.
//   simulate_click(x, y, bounds)        — hit-test room rows; updates selection.
//
// Lifetime contract:
//   LobbyBrowser is default-constructible and holds a non-owning raw pointer to
//   a cd::network::lobby::Lobby.  The caller must ensure the Lobby outlives the
//   panel, or call set_lobby(nullptr) before the panel is destroyed.
//
// MOMENT: A multiplayer dev runs the editor with the lobby server live, sees
// rooms appear/disappear in real-time as players create/join/leave — no
// external network monitor needed.
// =============================================================================
#pragma once

#include <cd/network/lobby/Lobby.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>
#include <optional>

namespace cd::editor::panel::lobby_browser
{

// ---------------------------------------------------------------------------
// LobbyBrowser
// ---------------------------------------------------------------------------
class LobbyBrowser
{
public:
    // Default-constructible; starts detached (no lobby, no selection).
    LobbyBrowser() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind the Lobby to visualize.  nullptr detaches.
    /// The pointer is stored non-owning; the caller must ensure it outlives the
    /// panel, or call set_lobby(nullptr) before the object is destroyed.
    void set_lobby(const cd::network::lobby::Lobby* lobby) noexcept;

    /// Returns the RoomId of the currently selected row, or std::nullopt when
    /// nothing is selected or no lobby is bound.
    [[nodiscard]] std::optional<uint64_t> selected_room_id() const noexcept;

    /// Perform a hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the row whose bounding rect contains (x, y).
    /// Does nothing when no lobby is bound or there are no rooms.
    void simulate_click(float x, float y,
                        const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. One row per active RoomState:
    ///        a. Row background (selected = accent-tinted, normal = surface_hover).
    ///        b. Left-edge game-mode accent bar.
    ///        c. Room name label rect (wide, text_dim colour).
    ///        d. Player-count fill bar (filled = current players / max_players).
    ///        e. Lock icon quad (accent_warning) when the room has a passcode.
    ///   4. Empty-state placeholder row when no rooms are available.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Bound data (non-owning) --------------------------------------------

    const cd::network::lobby::Lobby* lobby_ { nullptr };

    // ---- Selection ----------------------------------------------------------

    std::optional<cd::network::lobby::RoomId> selected_ {};

    // ---- Layout / geometry constants ----------------------------------------

    static constexpr float kPad      = 6.0F;   ///< Horizontal/vertical padding.
    static constexpr float kBarH     = 4.0F;   ///< Title separator bar height.
    static constexpr float kRowH     = 20.0F;  ///< Height of each room row.
    static constexpr float kRowGap   = 2.0F;   ///< Gap between rows.
    static constexpr float kEdgeBarW = 4.0F;   ///< Left-edge game-mode indicator width.
    static constexpr float kLockW    = 10.0F;  ///< Lock icon quad width.
    static constexpr float kLockH    = 12.0F;  ///< Lock icon quad height.
};

}  // namespace cd::editor::panel::lobby_browser
