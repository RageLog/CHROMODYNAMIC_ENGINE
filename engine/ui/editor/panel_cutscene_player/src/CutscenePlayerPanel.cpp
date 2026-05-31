// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_cutscene_player/src/CutscenePlayerPanel.cpp
//
// phase617 — cd::editor::panel::cutscene_player  implementation
// =============================================================================
#include <cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp>

#include <algorithm>
#include <cstddef>

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

    // ---- Per-phase block layout ----------------------------------------------
    // Compute total duration for proportional sizing.
    {
        float total_ms = 0.0F;
        for (const auto& phase : cutscene_.phases)
            total_ms += phase.duration_ms;

        constexpr float kPhaseRowH  = 20.0F;
        constexpr float kDotR       =  3.0F;
        constexpr float kMinBlockW  =  4.0F;

        if (total_ms > 0.0F && !cutscene_.phases.empty())
        {
            float block_x = bounds.x + kPad;

            for (std::size_t i = 0U; i < cutscene_.phases.size(); ++i)
            {
                const auto& phase    = cutscene_.phases[i];
                const float fraction = phase.duration_ms / total_ms;
                const float block_w  = std::max(kMinBlockW, row_w * fraction);
                const bool  active   = (i == active_phase);

                // Phase block background.
                const cd::ui::renderer::Color block_col =
                    active
                        ? cd::ui::renderer::Color { theme.accent.r, theme.accent.g, theme.accent.b, 200U }
                        : cd::ui::renderer::Color { theme.surface_hover.r, theme.surface_hover.g,
                                                    theme.surface_hover.b, theme.surface_hover.a };

                batcher.quad(block_x, cursor_y, block_w, kPhaseRowH, block_col);

                // Event marker dots within the phase block.
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

    // ---- Global timeline scrubber -------------------------------------------
    // A full-width track; the playhead is drawn as a vertical bar at the
    // current position within the active phase.
    {
        constexpr float kScrubH = 18.0F;

        // Sub-separator.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kBarH,
                     cd::ui::renderer::Color {
                         theme.accent.r, theme.accent.g, theme.accent.b, 80U });
        cursor_y += kBarH + kPad * 0.5F;

        // Scrubber track background.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kScrubH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r, theme.surface_hover.g,
                         theme.surface_hover.b, theme.surface_hover.a });

        // Playhead: compute global position proportionally.
        if (!cutscene_.phases.empty())
        {
            float total_ms = 0.0F;
            for (const auto& phase : cutscene_.phases)
                total_ms += phase.duration_ms;

            if (total_ms > 0.0F)
            {
                // Accumulate completed-phase time + current offset.
                float elapsed_ms = offset_ms;
                for (std::size_t i = 0U; i < active_phase && i < cutscene_.phases.size(); ++i)
                    elapsed_ms += cutscene_.phases[i].duration_ms;

                const float norm_head = std::clamp(elapsed_ms / total_ms, 0.0F, 1.0F);

                // Draw playhead as a 2-pixel wide strip.
                constexpr float kHeadW = 2.0F;
                batcher.quad(bounds.x + kPad + row_w * norm_head - kHeadW * 0.5F,
                             cursor_y,
                             kHeadW, kScrubH,
                             cd::ui::renderer::Color { 100U, 200U, 255U, 240U });
            }
        }

        cursor_y += kScrubH + kPad;
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
    }
}

}  // namespace cd::editor::panel::cutscene_player
