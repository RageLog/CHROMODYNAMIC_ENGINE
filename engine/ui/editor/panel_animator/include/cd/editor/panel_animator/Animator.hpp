// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_animator/Animator.hpp
//
// phase556-557-558 — cd::editor::panel::animator  (panel_animator library)
//
// Animation clip browser + timeline scrubber panel.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Renders:
//
//   * A coloured panel background.
//   * A separator bar under the title area.
//   * A clip-browser list of registered ClipId entries (accent bars with
//     the active clip highlighted in a contrasting colour).
//   * A timeline scrubber bar showing the current playback position as a
//     filled rectangle within the clip's normalised [0..1] range.
//   * A play/pause indicator strip and a loop indicator strip.
//
// State API:
//   set_clip(ClipId)                    — select the active clip (kInvalidClipId = none).
//   clip_id() const                     — returns the active ClipId.
//   add_clip(ClipId)                    — register a clip in the browser list.
//   remove_clip(ClipId)                 — deregister a clip from the browser list.
//   clip_count() const                  — number of registered clips.
//   set_time(float)                     — set current-time in seconds (clamped >= 0).
//   time() const                        — returns current time in seconds.
//   set_duration(float)                 — set the clip duration in seconds (>= 0).
//   duration() const                    — returns the current duration.
//   set_playing(bool)                   — set play/pause state.
//   is_playing() const                  — returns the current play state.
//   set_looping(bool)                   — set loop toggle state.
//   is_looping() const                  — returns the current loop state.
//
// ClipId is a lightweight opaque 32-bit identifier (same idiom as MaterialId in
// panel_material_editor). Resolving a ClipId to an actual AnimationClip is the
// caller's responsibility.
//
// Lifetime contract:
//   Animator is default-constructible and owns no heap storage beyond an
//   internal std::vector<ClipId> for the browser list.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>
#include <vector>

namespace cd::editor::panel::animator
{

// ---------------------------------------------------------------------------
// ClipId — lightweight opaque handle for an animation clip.
// ---------------------------------------------------------------------------
using ClipId = std::uint32_t;

/// Sentinel value meaning "no clip selected".
inline constexpr ClipId kInvalidClipId = 0U;

// ---------------------------------------------------------------------------
// Animator
// ---------------------------------------------------------------------------
class Animator
{
public:
    // Default-constructible; starts with no active clip and stopped state.
    Animator() noexcept = default;

    // ---- Clip browser API ---------------------------------------------------

    /// Select the active clip. Pass kInvalidClipId (or 0) to clear the
    /// selection. The panel stores the id but performs no asset-system lookup.
    void set_clip(ClipId id) noexcept;

    /// Returns the currently active ClipId (kInvalidClipId if none).
    [[nodiscard]] ClipId clip_id() const noexcept;

    /// Register a clip in the browser list. Duplicate ids are ignored.
    void add_clip(ClipId id);

    /// Remove a clip from the browser list. No-op if the id is not present.
    void remove_clip(ClipId id);

    /// Returns the number of clips registered in the browser list.
    [[nodiscard]] std::size_t clip_count() const noexcept;

    // ---- Timeline scrubber API ----------------------------------------------

    /// Set the current playback position in seconds. Values below 0 are
    /// clamped to 0.
    void set_time(float seconds) noexcept;

    /// Returns the current playback position in seconds.
    [[nodiscard]] float time() const noexcept;

    /// Set the active clip's duration in seconds. Values below 0 are clamped
    /// to 0. A duration of 0 makes the scrubber collapse to an empty bar.
    void set_duration(float seconds) noexcept;

    /// Returns the current clip duration in seconds.
    [[nodiscard]] float duration() const noexcept;

    // ---- Playback control API -----------------------------------------------

    /// Set the play/pause state.
    void set_playing(bool playing) noexcept;

    /// Returns true when the animator is in the playing state.
    [[nodiscard]] bool is_playing() const noexcept;

    /// Set the loop toggle state.
    void set_looping(bool looping) noexcept;

    /// Returns true when looping is enabled.
    [[nodiscard]] bool is_looping() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders the animator panel background, a separator, a clip-browser list,
    /// a timeline scrubber bar, and play/loop indicator strips.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    ClipId               clip_id_  { kInvalidClipId };  ///< Active clip.
    std::vector<ClipId>  clips_    {};                   ///< Browser list.
    float                time_     { 0.0F };             ///< Current time (s).
    float                duration_ { 0.0F };             ///< Clip duration (s).
    bool                 playing_  { false };            ///< Play state.
    bool                 looping_  { false };            ///< Loop toggle.
};

}  // namespace cd::editor::panel::animator
