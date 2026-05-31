// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_animator/src/Animator.cpp
//
// phase556-557-558 — cd::editor::panel::animator  implementation
// =============================================================================
#include <cd/editor/panel_animator/Animator.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::editor::panel::animator
{

// ---------------------------------------------------------------------------
// Clip browser API
// ---------------------------------------------------------------------------

void Animator::set_clip(ClipId id) noexcept
{
    clip_id_ = id;
}

ClipId Animator::clip_id() const noexcept
{
    return clip_id_;
}

void Animator::add_clip(ClipId id)
{
    // Ignore duplicates.
    if (std::find(clips_.begin(), clips_.end(), id) == clips_.end())
        clips_.push_back(id);
}

void Animator::remove_clip(ClipId id)
{
    clips_.erase(std::remove(clips_.begin(), clips_.end(), id), clips_.end());
}

std::size_t Animator::clip_count() const noexcept
{
    return clips_.size();
}

// ---------------------------------------------------------------------------
// Timeline scrubber API
// ---------------------------------------------------------------------------

void Animator::set_time(float seconds) noexcept
{
    time_ = (seconds < 0.0F) ? 0.0F : seconds;
}

float Animator::time() const noexcept
{
    return time_;
}

void Animator::set_duration(float seconds) noexcept
{
    duration_ = (seconds < 0.0F) ? 0.0F : seconds;
}

float Animator::duration() const noexcept
{
    return duration_;
}

// ---------------------------------------------------------------------------
// Playback control API
// ---------------------------------------------------------------------------

void Animator::set_playing(bool playing) noexcept
{
    playing_ = playing;
}

bool Animator::is_playing() const noexcept
{
    return playing_;
}

void Animator::set_looping(bool looping) noexcept
{
    looping_ = looping;
}

bool Animator::is_looping() const noexcept
{
    return looping_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void Animator::draw(cd::ui::renderer::DrawBatcher& batcher,
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

    constexpr float kRowH  = 18.0F;
    constexpr float kPad   = 6.0F;
    constexpr float kBarH  = 4.0F;
    const float     row_w  = bounds.w - 2.0F * kPad;

    // Separator bar under the title area (accent colour).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // ---- Clip browser list --------------------------------------------------
    // Each registered clip is drawn as a narrow bar. The active clip uses the
    // accent colour at full alpha; inactive clips are dimmed.
    {
        constexpr float kClipBarH = 14.0F;
        constexpr float kClipGap  = 2.0F;

        for (const ClipId cid : clips_)
        {
            // Check that we don't overflow the panel vertically.
            if ((cursor_y + kClipBarH) > (bounds.y + bounds.h - kPad))
                break;

            const bool active = (cid == clip_id_) && (clip_id_ != kInvalidClipId);

            // Track background.
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w, kClipBarH,
                         cd::ui::renderer::Color {
                             theme.surface_hover.r,
                             theme.surface_hover.g,
                             theme.surface_hover.b,
                             theme.surface_hover.a });

            // Highlight: active = accent colour, inactive = dim.
            if (active)
            {
                batcher.quad(bounds.x + kPad, cursor_y,
                             row_w, kClipBarH,
                             cd::ui::renderer::Color {
                                 theme.accent.r,
                                 theme.accent.g,
                                 theme.accent.b,
                                 180U });
            }
            else
            {
                batcher.quad(bounds.x + kPad, cursor_y,
                             6.0F, kClipBarH,
                             cd::ui::renderer::Color {
                                 theme.text_dim.r,
                                 theme.text_dim.g,
                                 theme.text_dim.b,
                                 120U });
            }

            cursor_y += kClipBarH + kClipGap;
        }

        // Reserve at least one row of space when the list is empty.
        if (clips_.empty())
        {
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w * 0.4F, kClipBarH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r,
                             theme.text_dim.g,
                             theme.text_dim.b,
                             80U });
            cursor_y += kClipBarH + kClipGap;
        }

        cursor_y += kPad;
    }

    // ---- Timeline scrubber --------------------------------------------------
    // A full-width track with a filled rectangle representing current-time / duration.
    {
        constexpr float kScrubH = kRowH;

        // Separator before the scrubber.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kBarH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         80U });
        cursor_y += kBarH + kPad * 0.5F;

        // Track background.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kScrubH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });

        // Fill: normalised playback position.
        if (duration_ > 0.0F)
        {
            const float norm = std::clamp(time_ / duration_, 0.0F, 1.0F);
            if (norm > 0.0F)
            {
                batcher.quad(bounds.x + kPad, cursor_y,
                             row_w * norm, kScrubH,
                             cd::ui::renderer::Color { 100U, 200U, 255U, 220U });
            }
        }

        cursor_y += kScrubH + kPad;
    }

    // ---- Play/pause indicator -----------------------------------------------
    // A small coloured strip: green = playing, grey = paused.
    {
        constexpr float kIndicatorW = 30.0F;
        constexpr float kIndicatorH = kRowH * 0.6F;

        const cd::ui::renderer::Color play_col =
            playing_
                ? cd::ui::renderer::Color { 80U,  220U, 80U,  220U }
                : cd::ui::renderer::Color { 140U, 140U, 140U, 180U };

        batcher.quad(bounds.x + kPad, cursor_y,
                     kIndicatorW, kIndicatorH,
                     play_col);

        // ---- Loop indicator -------------------------------------------------
        // Drawn immediately to the right of the play indicator.
        if (is_looping())
        {
            batcher.quad(bounds.x + kPad + kIndicatorW + kPad, cursor_y,
                         kIndicatorW, kIndicatorH,
                         cd::ui::renderer::Color { 255U, 200U, 60U, 220U });
        }
    }
}

}  // namespace cd::editor::panel::animator
