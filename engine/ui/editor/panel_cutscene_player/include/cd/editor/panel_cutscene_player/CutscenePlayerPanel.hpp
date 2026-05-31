// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp
//
// phase617 — cd::editor::panel::cutscene_player  (panel_cutscene_player library)
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
//   * Current offset display strip (visual, not text — no font dependency).
//
// State API:
//   set_cutscene(const Cutscene&)        — copy + cache the timeline asset.
//   set_player(CutscenePlayer*)          — observe the runtime player (nullable).
//   cutscene_phase_count() const         — number of phases in the cached cutscene.
//   player() const                       — returns the observed player pointer (may be null).
//
// Lifetime contract:
//   CutscenePlayerPanel is default-constructible; owns a copy of the cutscene
//   and a non-owning raw pointer to the player.  The caller is responsible for
//   ensuring the player outlives the panel.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/game/cutscene_player/CutscenePlayer.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>

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

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders: background, separator, per-phase blocks, event marker dots,
    /// global scrubber, playhead indicator, and Play/Pause/Stop/Skip strips.
    ///
    /// Thread-safety: must be called from the render thread only.
    /// If set_player() returned a non-null pointer, is_playing() /
    /// current_phase_index() / current_offset_ms() are sampled once per call.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    cd::game::cutscene_player::Cutscene         cutscene_;  ///< Cached timeline asset.
    cd::game::cutscene_player::CutscenePlayer*  player_ { nullptr };  ///< Non-owning observer.
};

}  // namespace cd::editor::panel::cutscene_player
