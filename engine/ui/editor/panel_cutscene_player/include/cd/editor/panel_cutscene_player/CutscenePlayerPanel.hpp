// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp
//
// phase617 — cd::editor::panel::cutscene_player  (panel_cutscene_player library)
// phase703 — Save/Load JSON round-trip added (M15 W4B).
//
// Editor panel that drives cd::game::cutscene_player::CutscenePlayer.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Renders:
//
//   * A coloured panel background + separator bar.
//   * Per-phase block layout: one coloured block per CutscenePhase, sized
//     proportionally to duration_ms. The active phase is highlighted.
//   * Event marker dots drawn at scaled offsets within each phase block.
//   * Timeline scrubber: a full-width track with a playhead line at the
//     current global playhead position.
//   * Play / Pause / Stop / Skip control buttons (coloured indicator strips).
//   * Save / Load button strips — persist the cutscene as a JSON file at the
//     default editor path (~/.cd_editor/cutscenes/<cutscene_id>.json).
//   * Current offset display strip (visual, not text — no font dependency).
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

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders: background, separator, per-phase blocks, event marker dots,
    /// global scrubber, playhead indicator, Play/Pause/Stop/Skip strips, and
    /// Save/Load button strips.
    ///
    /// Thread-safety: must be called from the render thread only.
    /// If set_player() returned a non-null pointer, is_playing() /
    /// current_phase_index() / current_offset_ms() are sampled once per call.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    /// Derive the default save path from the current cutscene_id and the
    /// platform editor config directory.
    [[nodiscard]] std::filesystem::path default_save_path() const;

    cd::game::cutscene_player::Cutscene         cutscene_;              ///< Cached timeline asset.
    cd::game::cutscene_player::CutscenePlayer*  player_       { nullptr }; ///< Non-owning observer.
    std::filesystem::path                       last_path_;             ///< Last save/load path.
    bool                                        last_save_ok_ { false }; ///< Most recent save result.
    bool                                        last_load_ok_ { false }; ///< Most recent load result.
};

}  // namespace cd::editor::panel::cutscene_player
