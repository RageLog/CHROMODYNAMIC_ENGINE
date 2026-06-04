// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_cutscene_player/src/CutscenePlayerPanel.cpp
//
// phase617 — cd::editor::panel::cutscene_player  implementation
// phase703 — Save/Load JSON round-trip added (M15 W4B).
// phase741 — Pan/zoom Sprint-2: zoom_factor + pan_offset_x + tick_input +
//             scrollbar rendering; all timeline elements scale + offset.
// =============================================================================
#include <cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <cstdlib>
#else
  #include <cstdlib>
#endif

namespace cd::editor::panel::cutscene_player
{

// ---------------------------------------------------------------------------
// Cutscene binding API
// ---------------------------------------------------------------------------

void CutscenePlayerPanel::set_cutscene(const cd::game::cutscene_player::Cutscene& cutscene)
{
    cutscene_ = cutscene;
}

std::size_t CutscenePlayerPanel::cutscene_phase_count() const noexcept
{
    return cutscene_.phases.size();
}

// ---------------------------------------------------------------------------
// Player binding API
// ---------------------------------------------------------------------------

void CutscenePlayerPanel::set_player(cd::game::cutscene_player::CutscenePlayer* player) noexcept
{
    player_ = player;
}

cd::game::cutscene_player::CutscenePlayer* CutscenePlayerPanel::player() const noexcept
{
    return player_;
}

// ---------------------------------------------------------------------------
// Save / Load API (phase 703)
// ---------------------------------------------------------------------------

namespace
{

/// Portable getenv — mirrors cd::editor::cdproj / cd::game_save pattern.
[[nodiscard]] std::optional<std::string> get_env_str(const char* name)
{
#if defined(_WIN32)
    char*       buf = nullptr;
    std::size_t sz  = 0;
    if (::_dupenv_s(&buf, &sz, name) != 0 || buf == nullptr)
    {
        return std::nullopt;
    }
    std::string value(buf);
    std::free(buf);  // NOLINT(cppcoreguidelines-no-malloc)
    if (value.empty()) { return std::nullopt; }
    return value;
#else
    const char* p = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
    if (p == nullptr || p[0] == '\0') { return std::nullopt; }
    return std::string(p);
#endif
}

}  // namespace

std::filesystem::path CutscenePlayerPanel::default_save_path() const
{
    namespace fs = std::filesystem;

    const std::string& cs_id = cutscene_.cutscene_id.empty()
                             ? std::string("unnamed")
                             : cutscene_.cutscene_id;
    const std::string filename = cs_id + ".json";

#if defined(_WIN32)
    if (auto base = get_env_str("APPDATA"))
    {
        return fs::path(*base) / "cd_editor" / "cutscenes" / filename;
    }
    if (auto base = get_env_str("LOCALAPPDATA"))
    {
        return fs::path(*base) / "cd_editor" / "cutscenes" / filename;
    }
    if (auto up = get_env_str("USERPROFILE"))
    {
        return fs::path(*up) / "AppData" / "Roaming" / "cd_editor" / "cutscenes" / filename;
    }
    return fs::temp_directory_path() / "cd_editor" / "cutscenes" / filename;
#elif defined(__APPLE__)
    if (auto home = get_env_str("HOME"))
    {
        return fs::path(*home) / "Library" / "Application Support"
                              / "cd_editor" / "cutscenes" / filename;
    }
    return fs::temp_directory_path() / "cd_editor" / "cutscenes" / filename;
#else
    if (auto xdg = get_env_str("XDG_CONFIG_HOME"))
    {
        return fs::path(*xdg) / "cd_editor" / "cutscenes" / filename;
    }
    if (auto home = get_env_str("HOME"))
    {
        return fs::path(*home) / ".config" / "cd_editor" / "cutscenes" / filename;
    }
    return fs::temp_directory_path() / "cd_editor" / "cutscenes" / filename;
#endif
}

bool CutscenePlayerPanel::save()
{
    last_path_    = default_save_path();
    last_save_ok_ = cd::game::cutscene_player::save_to_json(cutscene_, last_path_);
    return last_save_ok_;
}

bool CutscenePlayerPanel::load()
{
    last_path_ = default_save_path();
    auto result = cd::game::cutscene_player::load_from_json(last_path_);
    last_load_ok_ = result.has_value();
    if (last_load_ok_)
    {
        cutscene_ = std::move(*result);
    }
    return last_load_ok_;
}

const std::filesystem::path& CutscenePlayerPanel::last_save_path() const noexcept
{
    return last_path_;
}

bool CutscenePlayerPanel::last_save_ok() const noexcept
{
    return last_save_ok_;
}

bool CutscenePlayerPanel::last_load_ok() const noexcept
{
    return last_load_ok_;
}

// ---------------------------------------------------------------------------
// Pan / Zoom API (phase 741)
// ---------------------------------------------------------------------------

float CutscenePlayerPanel::zoom_factor() const noexcept
{
    return zoom_factor_;
}

float CutscenePlayerPanel::pan_offset_x() const noexcept
{
    return pan_offset_x_;
}

void CutscenePlayerPanel::set_zoom_factor(float zoom) noexcept
{
    zoom_factor_ = std::clamp(zoom, kZoomMin, kZoomMax);
}

void CutscenePlayerPanel::set_pan_offset_x(float offset_x, float track_width) noexcept
{
    pan_offset_x_ = offset_x;
    clamp_pan_(track_width);
}

void CutscenePlayerPanel::reset_view() noexcept
{
    zoom_factor_  = 1.0F;
    pan_offset_x_ = 0.0F;
    panning_      = false;
}

void CutscenePlayerPanel::clamp_pan_(float track_w) noexcept
{
    // The zoomed timeline has pixel width = track_w * zoom_factor_.
    // Allow panning up to (zoomed_width - track_w) to the right (negative offset).
    // The pan is stored as a positive-right signed value applied as
    //   block_x = timeline_origin - pan_offset_x_
    // so:
    //   pan_min = 0                              (left edge at origin, no overscroll)
    //   pan_max = max(0, track_w * zoom_factor_ - track_w)
    const float zoomed_w = track_w * zoom_factor_;
    const float max_pan  = std::max(0.0F, zoomed_w - track_w);
    pan_offset_x_ = std::clamp(pan_offset_x_, 0.0F, max_pan);
}

void CutscenePlayerPanel::tick_input(const cd::ui::widgets::PointerState& pointer,
                                     bool                                  middle_down,
                                     float                                 wheel_delta,
                                     bool                                  shift_held,
                                     const cd::ui::widgets::Rect&          bounds) noexcept
{
    const float track_w = bounds.w;  // use full panel width as track reference

    // ---- Zoom via mouse wheel (cursor-anchored) ------------------------------
    if (wheel_delta != 0.0F)
    {
        // Cursor position relative to the track origin.
        const float cursor_x_in_track = pointer.mouse_x - bounds.x;

        // Map cursor to a normalised position within the zoomed timeline:
        //   norm = (cursor_x_in_track + pan_offset_x_) / (track_w * zoom_factor_)
        // After zoom the same norm must still be at cursor_x_in_track:
        //   new_pan = norm * new_zoomed_w - cursor_x_in_track
        const float old_zoomed_w = track_w * zoom_factor_;
        const float norm_at_cursor = (old_zoomed_w > 0.0F)
            ? (cursor_x_in_track + pan_offset_x_) / old_zoomed_w
            : 0.0F;

        // Apply zoom step for each tick (fractional deltas handled proportionally).
        const float factor = (wheel_delta > 0.0F)
            ? std::pow(kZoomStep, wheel_delta)
            : (1.0F / std::pow(kZoomStep, -wheel_delta));

        zoom_factor_ = std::clamp(zoom_factor_ * factor, kZoomMin, kZoomMax);

        const float new_zoomed_w  = track_w * zoom_factor_;
        pan_offset_x_ = norm_at_cursor * new_zoomed_w - cursor_x_in_track;
        clamp_pan_(track_w);
    }

    // ---- Pan via middle-button drag or shift + left-drag --------------------
    const bool pan_button_down = middle_down || (shift_held && pointer.left_down);

    if (pan_button_down)
    {
        if (panning_)
        {
            const float delta_x = pointer.mouse_x - prev_mouse_x_;
            // Dragging right means panning left → subtract delta from pan offset.
            pan_offset_x_ -= delta_x;
            clamp_pan_(track_w);
        }
        panning_ = true;
    }
    else
    {
        panning_ = false;
    }

    prev_mouse_x_ = pointer.mouse_x;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void CutscenePlayerPanel::draw(cd::ui::renderer::DrawBatcher& batcher,
                                const cd::ui::widgets::Theme&  theme,
                                const cd::ui::widgets::Rect&   bounds) const
{
    // Background fill.
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // Zoomed track width and panned left-edge for all timeline-space elements.
    const float zoomed_row_w = row_w * zoom_factor_;
    const float track_origin = bounds.x + kPad - pan_offset_x_;

    // Separator bar under the title area (accent colour).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // Sample player state once.
    const bool        playing      = (player_ != nullptr) && player_->is_playing();
    const std::size_t active_phase = (player_ != nullptr) ? player_->current_phase_index() : 0U;
    const float       offset_ms    = (player_ != nullptr) ? player_->current_offset_ms()   : 0.0F;

    // Push a scissor so zoomed/panned blocks don't bleed outside the track area.
    const cd::ui::renderer::ScissorRect track_scissor {
        static_cast<std::int32_t>(bounds.x + kPad),
        static_cast<std::int32_t>(bounds.y),
        static_cast<std::uint32_t>(std::max(0.0F, row_w)),
        static_cast<std::uint32_t>(std::max(0.0F, bounds.h))
    };
    batcher.push_scissor(track_scissor);

    // ---- Per-phase block layout (zoomed + panned) ---------------------------
    float total_ms = 0.0F;
    for (const auto& phase : cutscene_.phases)
        total_ms += phase.duration_ms;

    {
        constexpr float kPhaseRowH  = 20.0F;
        constexpr float kDotR       =  3.0F;
        constexpr float kMinBlockW  =  4.0F;

        if (total_ms > 0.0F && !cutscene_.phases.empty())
        {
            float block_x = track_origin;

            for (std::size_t i = 0U; i < cutscene_.phases.size(); ++i)
            {
                const auto& phase    = cutscene_.phases[i];
                const float fraction = phase.duration_ms / total_ms;
                const float block_w  = std::max(kMinBlockW, zoomed_row_w * fraction);
                const bool  active   = (i == active_phase);

                // Phase block background.
                const cd::ui::renderer::Color block_col =
                    active
                        ? cd::ui::renderer::Color { theme.accent.r, theme.accent.g, theme.accent.b, 200U }
                        : cd::ui::renderer::Color { theme.surface_hover.r, theme.surface_hover.g,
                                                    theme.surface_hover.b, theme.surface_hover.a };

                batcher.quad(block_x, cursor_y, block_w, kPhaseRowH, block_col);

                // Event marker dots within the phase block (zoomed position).
                for (const auto& evt : phase.events)
                {
                    if (phase.duration_ms <= 0.0F)
                        break;

                    const float norm_evt = std::clamp(evt.offset_ms / phase.duration_ms, 0.0F, 1.0F);
                    const float dot_cx   = block_x + block_w * norm_evt;
                    const float dot_y    = cursor_y + kPhaseRowH * 0.5F - kDotR;

                    // Draw dot as a small square (batcher only supports quads).
                    batcher.quad(dot_cx - kDotR, dot_y,
                                 kDotR * 2.0F, kDotR * 2.0F,
                                 cd::ui::renderer::Color { 255U, 220U, 60U, 220U });
                }

                block_x += block_w;
            }

            cursor_y += kPhaseRowH + kPad;
        }
        else if (cutscene_.phases.empty())
        {
            // Empty cutscene placeholder bar.
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w * 0.6F, 14.0F,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 80U });
            cursor_y += 14.0F + kPad;
        }
    }

    // ---- Global timeline scrubber (zoomed + panned) -------------------------
    // A track fixed to the unzoomed width; the playhead position is scaled.
    {
        constexpr float kScrubH = 18.0F;

        // Sub-separator.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kBarH,
                     cd::ui::renderer::Color {
                         theme.accent.r, theme.accent.g, theme.accent.b, 80U });
        cursor_y += kBarH + kPad * 0.5F;

        // Scrubber track background (unzoomed width — always full-width).
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kScrubH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r, theme.surface_hover.g,
                         theme.surface_hover.b, theme.surface_hover.a });

        // Playhead: compute global position proportionally, then apply zoom + pan.
        if (!cutscene_.phases.empty() && total_ms > 0.0F)
        {
            // Accumulate completed-phase time + current offset.
            float elapsed_ms = offset_ms;
            for (std::size_t i = 0U; i < active_phase && i < cutscene_.phases.size(); ++i)
                elapsed_ms += cutscene_.phases[i].duration_ms;

            const float norm_head = std::clamp(elapsed_ms / total_ms, 0.0F, 1.0F);

            // Map norm into the zoomed timeline; subtract pan to get screen pos.
            const float zoomed_head_x = track_origin + zoomed_row_w * norm_head;

            // Only draw the playhead if it is within the visible scrubber track.
            constexpr float kHeadW = 2.0F;
            const float head_screen_x = zoomed_head_x - kHeadW * 0.5F;
            if (head_screen_x >= bounds.x + kPad &&
                head_screen_x < bounds.x + kPad + row_w)
            {
                batcher.quad(head_screen_x, cursor_y,
                             kHeadW, kScrubH,
                             cd::ui::renderer::Color { 100U, 200U, 255U, 240U });
            }
        }

        cursor_y += kScrubH + kPad;
    }

    batcher.pop_scissor();

    // ---- Scrollbar (phase 741) -----------------------------------------------
    // Shows the currently visible viewport window within the full zoomed timeline.
    // Only drawn when the timeline is actually zoomed in (zoom_factor_ > 1.0).
    {
        // Track.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kScrollbarH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r, theme.surface_hover.g,
                         theme.surface_hover.b, theme.surface_hover.a });

        if (zoom_factor_ > 1.0F)
        {
            // Thumb spans the fraction of the zoomed timeline that is visible:
            //   thumb_w = row_w / zoom_factor_
            //   thumb_x = (pan_offset_x_ / zoomed_row_w) * row_w
            const float thumb_w = std::max(4.0F, row_w / zoom_factor_);
            const float zoomed_w = row_w * zoom_factor_;
            const float thumb_x  = (zoomed_w > 0.0F)
                ? (pan_offset_x_ / zoomed_w) * row_w
                : 0.0F;

            batcher.quad(bounds.x + kPad + std::clamp(thumb_x, 0.0F, row_w - thumb_w),
                         cursor_y,
                         thumb_w, kScrollbarH,
                         cd::ui::renderer::Color { theme.accent.r, theme.accent.g,
                                                    theme.accent.b, 180U });
        }

        cursor_y += kScrollbarH + kPad;
    }

    // ---- Current offset display strip ----------------------------------------
    // A proportionally-filled mini-bar showing progress within the active phase.
    {
        constexpr float kOffsetH = 8.0F;

        float phase_dur = 0.0F;
        if (active_phase < cutscene_.phases.size())
            phase_dur = cutscene_.phases[active_phase].duration_ms;

        // Track.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kOffsetH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r, theme.surface_hover.g,
                         theme.surface_hover.b, theme.surface_hover.a });

        // Fill: offset within the current phase.
        if (phase_dur > 0.0F)
        {
            const float norm_off = std::clamp(offset_ms / phase_dur, 0.0F, 1.0F);
            if (norm_off > 0.0F)
            {
                batcher.quad(bounds.x + kPad, cursor_y,
                             row_w * norm_off, kOffsetH,
                             cd::ui::renderer::Color { 80U, 220U, 180U, 200U });
            }
        }

        cursor_y += kOffsetH + kPad;
    }

    // ---- Play / Pause / Stop / Skip button strips ----------------------------
    {
        constexpr float kBtnW = 28.0F;
        constexpr float kBtnH = 16.0F;
        constexpr float kGap  =  4.0F;

        // Play / Pause indicator: green when playing, grey when paused.
        const cd::ui::renderer::Color play_col =
            playing
                ? cd::ui::renderer::Color {  80U, 220U,  80U, 220U }
                : cd::ui::renderer::Color { 140U, 140U, 140U, 180U };
        batcher.quad(bounds.x + kPad, cursor_y, kBtnW, kBtnH, play_col);

        // Stop button strip.
        batcher.quad(bounds.x + kPad + (kBtnW + kGap), cursor_y, kBtnW, kBtnH,
                     cd::ui::renderer::Color { 220U, 80U, 80U, 200U });

        // Skip button strip (skippable cutscenes only — greyed out otherwise).
        const bool skippable = cutscene_.can_skip;
        const cd::ui::renderer::Color skip_col =
            skippable
                ? cd::ui::renderer::Color { 255U, 180U,  50U, 200U }
                : cd::ui::renderer::Color { 100U, 100U, 100U, 120U };
        batcher.quad(bounds.x + kPad + 2.0F * (kBtnW + kGap), cursor_y, kBtnW, kBtnH, skip_col);

        cursor_y += kBtnH + kPad;
    }

    // Suppress unused-variable warning for cursor_y after last strip.
    (void)cursor_y;

    // ---- Save / Load button strips (phase 703) --------------------------------
    // Visual-only representation: Save = teal strip, Load = lavender strip.
    // Status tint: bright when last op succeeded, dim when failed or never called.
    {
        constexpr float kBtnW = 36.0F;
        constexpr float kBtnH = 14.0F;
        constexpr float kGap  =  6.0F;

        // cursor_y was advanced by the last strip; reuse it here.
        const float save_y = bounds.y + bounds.h - kBtnH - kPad;

        // Save strip — teal; brightens on last_save_ok_.
        const cd::ui::renderer::Color save_col =
            last_save_ok_
                ? cd::ui::renderer::Color {  40U, 210U, 190U, 230U }
                : cd::ui::renderer::Color {  40U, 140U, 130U, 150U };
        batcher.quad(bounds.x + kPad, save_y, kBtnW, kBtnH, save_col);

        // Load strip — lavender; brightens on last_load_ok_.
        const cd::ui::renderer::Color load_col =
            last_load_ok_
                ? cd::ui::renderer::Color { 160U, 130U, 230U, 230U }
                : cd::ui::renderer::Color { 110U,  90U, 160U, 150U };
        batcher.quad(bounds.x + kPad + (kBtnW + kGap), save_y, kBtnW, kBtnH, load_col);
    }
}

}  // namespace cd::editor::panel::cutscene_player
