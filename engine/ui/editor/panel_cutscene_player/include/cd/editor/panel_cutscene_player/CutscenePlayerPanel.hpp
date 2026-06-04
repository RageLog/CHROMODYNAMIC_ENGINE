// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp
//
// phase617 — cd::editor::panel::cutscene_player  (panel_cutscene_player library)
// phase703 — Save/Load JSON round-trip added (M15 W4B).
// phase741 — Pan/zoom Sprint-2: zoom_factor + pan_offset_x state,
//             mouse-wheel zoom (cursor-anchored), shift-drag pan,
//             scrollbar at the bottom of the timeline, all phase
//             blocks / event dots / scrubber scale + offset accordingly.
//
// Editor panel that drives cd::game::cutscene_player::CutscenePlayer.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Renders:
//
//   * A coloured panel background + separator bar.
//   * Per-phase block layout: one coloured block per CutscenePhase, sized
//     proportionally to duration_ms, scaled by zoom_factor and shifted by
//     pan_offset_x.  The active phase is highlighted.
//   * Event marker dots drawn at zoomed + panned offsets within each phase block.
//   * Timeline scrubber: a full-width track with a playhead line at the
//     current global playhead position (zoom + pan applied).
//   * Play / Pause / Stop / Skip control buttons (coloured indicator strips).
//   * Save / Load button strips — persist the cutscene as a JSON file at the
//     default editor path (~/.cd_editor/cutscenes/<cutscene_id>.json).
//   * Current offset display strip (visual, not text — no font dependency).
//   * Scrollbar at the bottom showing the visible viewport within the full
//     zoomed timeline.
//
// Pan / Zoom input:
//   tick_input(pointer, wheel_delta) must be called each frame with the
//   current PointerState and the mouse-wheel delta (positive = zoom in,
//   negative = zoom out).  The method updates zoom_factor and pan_offset_x.
//
//   * Mouse-wheel zooms around the cursor X position (anchor-preserving).
//   * Middle-button drag or shift+left-drag pans.
//   * zoom_factor is clamped to [kZoomMin, kZoomMax].
//   * pan_offset_x is clamped so the timeline can never be panned fully
//     out of the visible track area.
//
// State API:
//   set_cutscene(const Cutscene&)        — copy + cache the timeline asset.
//   set_player(CutscenePlayer*)          — observe the runtime player (nullable).
//   cutscene_phase_count() const         — number of phases in the cached cutscene.
//   player() const                       — returns the observed player pointer (may be null).
//   save() -> bool                       — write cached cutscene to the default JSON path.
//   load() -> bool                       — read from the default JSON path; replaces cache.
//   last_save_path() const               — the path most recently used by save() / load().
//   last_save_ok() const                 — true if the most recent save() call succeeded.
//   last_load_ok() const                 — true if the most recent load() call succeeded.
//   zoom_factor() const                  — current zoom (1.0 = 100%).
//   pan_offset_x() const                 — current horizontal pan offset in pixels.
//   set_zoom_factor(float)               — override zoom programmatically (clamped).
//   set_pan_offset_x(float)              — override pan programmatically (clamped per bounds).
//   reset_view()                         — restore zoom=1, pan=0.
//
// Default path (per CHROMODYNAMIC conventions, matches brief):
//
//   Windows : %APPDATA%\cd_editor\cutscenes\<cutscene_id>.json
//   macOS   : $HOME/Library/Application Support/cd_editor/cutscenes/<cutscene_id>.json
//   Linux   : $XDG_CONFIG_HOME/cd_editor/cutscenes/<cutscene_id>.json
//             or $HOME/.config/cd_editor/cutscenes/<cutscene_id>.json
//             or /tmp/cd_editor/cutscenes/<cutscene_id>.json
//
//   Falls back to std::filesystem::temp_directory_path() if none of the above
//   env-vars are set.
//
// Lifetime contract:
//   CutscenePlayerPanel is default-constructible; owns a copy of the cutscene
//   and a non-owning raw pointer to the player.  The caller is responsible for
//   ensuring the player outlives the panel.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/game/cutscene_player/CutsceneJson.hpp>
#include <cd/game/cutscene_player/CutscenePlayer.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace cd::editor::panel::cutscene_player
{

// ---------------------------------------------------------------------------
// CutscenePlayerPanel
// ---------------------------------------------------------------------------
class CutscenePlayerPanel
{
public:
    /// Zoom bounds for zoom_factor_.
    static constexpr float kZoomMin =  0.1F;
    static constexpr float kZoomMax = 10.0F;

    // Default-constructible; starts with an empty cutscene and null player.
    CutscenePlayerPanel() noexcept = default;

    // ---- Cutscene binding API ------------------------------------------------

    /// Copy and cache the cutscene timeline asset. The panel stores a copy; it
    /// does NOT hold a reference to the caller's Cutscene. The player pointer
    /// (if set) is not changed.
    void set_cutscene(const cd::game::cutscene_player::Cutscene& cutscene);

    /// Returns the number of phases in the currently cached cutscene.
    [[nodiscard]] std::size_t cutscene_phase_count() const noexcept;

    // ---- Player binding API -------------------------------------------------

    /// Observe a CutscenePlayer. Pass nullptr to detach. The panel reads
    /// is_playing(), current_phase_index(), and current_offset_ms() each draw.
    /// Ownership remains with the caller — the panel never deletes the pointer.
    void set_player(cd::game::cutscene_player::CutscenePlayer* player) noexcept;

    /// Returns the currently observed player pointer (may be nullptr).
    [[nodiscard]] cd::game::cutscene_player::CutscenePlayer* player() const noexcept;

    // ---- Save / Load API (phase 703) ----------------------------------------

    /// Serialize the cached cutscene to the default JSON path.
    /// The default path is derived from the cutscene_id and the platform's
    /// editor config directory (~/.cd_editor/cutscenes/<id>.json).
    /// Returns true on success, false on I/O error.
    /// Updates last_save_ok() and last_save_path() regardless of outcome.
    bool save();

    /// Load a Cutscene from the default JSON path (same derivation as save()).
    /// On success replaces the cached cutscene with the loaded data.
    /// Returns true on success, false on I/O error or parse failure.
    /// Updates last_load_ok() and last_save_path() regardless of outcome.
    bool load();

    /// The path most recently used by save() or load() (empty before first call).
    [[nodiscard]] const std::filesystem::path& last_save_path() const noexcept;

    /// True if the most recent save() call succeeded (false before first call).
    [[nodiscard]] bool last_save_ok() const noexcept;

    /// True if the most recent load() call succeeded (false before first call).
    [[nodiscard]] bool last_load_ok() const noexcept;

    // ---- Pan / Zoom API (phase 741) -----------------------------------------

    /// Returns the current zoom factor (default 1.0, range [kZoomMin, kZoomMax]).
    [[nodiscard]] float zoom_factor()  const noexcept;

    /// Returns the current horizontal pan offset in pixels (positive = panned right).
    [[nodiscard]] float pan_offset_x() const noexcept;

    /// Override zoom programmatically. Value is clamped to [kZoomMin, kZoomMax].
    void set_zoom_factor(float zoom) noexcept;

    /// Override pan programmatically. Value is clamped so the timeline cannot be
    /// scrolled fully out of view. Clamping uses `track_width` as the reference
    /// pixel width of the timeline track.
    void set_pan_offset_x(float offset_x, float track_width) noexcept;

    /// Reset zoom to 1.0 and pan to 0.
    void reset_view() noexcept;

    /// Process one frame of pointer input and update zoom_factor / pan_offset_x.
    ///
    ///   `pointer`     — current frame pointer state (position + left button).
    ///   `middle_down` — true while the middle mouse button is held this frame.
    ///   `wheel_delta` — mouse-wheel ticks this frame (positive = forward / zoom in,
    ///                   negative = backward / zoom out). One tick = 1.0.
    ///   `shift_held`  — true when Shift is held (enables left-button pan).
    ///   `bounds`      — the panel bounds rect (used for track-width clamping and
    ///                   cursor-anchor computation).
    ///
    /// Zoom: each wheel tick multiplies zoom_factor by kZoomStep (cursor-anchored).
    /// Pan:  middle-button drag or (shift + left-button) drag.
    void tick_input(const cd::ui::widgets::PointerState& pointer,
                    bool                                  middle_down,
                    float                                 wheel_delta,
                    bool                                  shift_held,
                    const cd::ui::widgets::Rect&          bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders: background, separator, per-phase blocks (zoomed + panned), event
    /// marker dots, global scrubber (zoomed + panned), playhead indicator,
    /// Play/Pause/Stop/Skip strips, Save/Load button strips, and a scrollbar that
    /// shows the visible viewport within the total zoomed timeline.
    ///
    /// Thread-safety: must be called from the render thread only.
    /// If set_player() returned a non-null pointer, is_playing() /
    /// current_phase_index() / current_offset_ms() are sampled once per call.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    /// Per-wheel-tick zoom multiplier.
    static constexpr float kZoomStep = 1.15F;
    /// Scrollbar height in pixels.
    static constexpr float kScrollbarH = 8.0F;

    /// Derive the default save path from the current cutscene_id and the
    /// platform editor config directory.
    [[nodiscard]] std::filesystem::path default_save_path() const;

    /// Clamp pan_offset_x_ so the zoomed timeline always partially overlaps
    /// the visible track. `track_w` is the available pixel width of the track.
    void clamp_pan_(float track_w) noexcept;

    cd::game::cutscene_player::Cutscene         cutscene_;              ///< Cached timeline asset.
    cd::game::cutscene_player::CutscenePlayer*  player_       { nullptr }; ///< Non-owning observer.
    std::filesystem::path                       last_path_;             ///< Last save/load path.
    bool                                        last_save_ok_ { false }; ///< Most recent save result.
    bool                                        last_load_ok_ { false }; ///< Most recent load result.

    // ---- Pan / zoom state (phase 741) ---------------------------------------
    float zoom_factor_   { 1.0F };   ///< Zoom multiplier in [kZoomMin, kZoomMax].
    float pan_offset_x_  { 0.0F };   ///< Horizontal pan offset in pixels (positive = right).
    float prev_mouse_x_  { 0.0F };   ///< Mouse X from previous tick_input frame (for drag delta).
    bool  panning_       { false };  ///< True while a pan-drag gesture is in progress.
};

}  // namespace cd::editor::panel::cutscene_player
