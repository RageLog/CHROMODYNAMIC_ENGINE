// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_lobby_browser/src/LobbyBrowser.cpp
//
// phase718 — cd::editor::panel::lobby_browser  implementation
// =============================================================================
#include <cd/editor/panel_lobby_browser/LobbyBrowser.hpp>

#include <algorithm>
#include <cstddef>

namespace cd::editor::panel::lobby_browser
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void LobbyBrowser::set_lobby(const cd::network::lobby::Lobby* lobby) noexcept
{
    lobby_ = lobby;
    // Clear selection when the lobby changes to avoid stale room ids.
    selected_.reset();
}

std::optional<uint64_t> LobbyBrowser::selected_room_id() const noexcept
{
    if (!selected_.has_value())
        return std::nullopt;
    return static_cast<uint64_t>(*selected_);
}

void LobbyBrowser::simulate_click(float x, float y,
                                  const cd::ui::widgets::Rect& bounds) noexcept
{
    if (lobby_ == nullptr)
        return;

    const std::span<const cd::network::lobby::RoomState> rooms =
        lobby_->active_rooms();
    if (rooms.empty())
        return;

    const float row_w    = bounds.w - 2.0F * kPad;
    float       cursor_y = bounds.y + kPad + kBarH + kPad;

    for (const cd::network::lobby::RoomState& rs : rooms)
    {
        // Bounding rect for this row.
        const float row_x = bounds.x + kPad;
        const float row_y = cursor_y;

        if (x >= row_x && x <= (row_x + row_w) &&
            y >= row_y && y <= (row_y + kRowH))
        {
            selected_ = rs.room_id;
            return;
        }

        cursor_y += kRowH + kRowGap;
        if (cursor_y > (bounds.y + bounds.h - kPad))
            break;
    }
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void LobbyBrowser::draw(cd::ui::renderer::DrawBatcher&  batcher,
                        const cd::ui::widgets::Theme&   theme,
                        const cd::ui::widgets::Rect&    bounds) const
{
    // 1. Background fill.
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    const float row_w    = bounds.w - 2.0F * kPad;
    const float content_x = bounds.x + kPad;

    // 2. Accent separator bar under title area.
    batcher.quad(content_x, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad + kBarH + kPad;

    // 3. Room rows (or empty-state placeholder).
    if (lobby_ == nullptr)
    {
        // Empty placeholder when no lobby is bound.
        batcher.quad(content_x, cursor_y,
                     row_w * 0.5F, kRowH,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         60U });
        return;
    }

    const std::span<const cd::network::lobby::RoomState> rooms =
        lobby_->active_rooms();

    if (rooms.empty())
    {
        // Empty placeholder when the lobby has no rooms.
        batcher.quad(content_x, cursor_y,
                     row_w * 0.4F, kRowH,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         60U });
        return;
    }

    for (const cd::network::lobby::RoomState& rs : rooms)
    {
        // Overflow guard — stop drawing if we're past the panel bottom.
        if ((cursor_y + kRowH) > (bounds.y + bounds.h - kPad))
            break;

        const bool is_selected =
            selected_.has_value() && (*selected_ == rs.room_id);

        // 3a. Row background.
        if (is_selected)
        {
            batcher.quad(content_x, cursor_y, row_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             80U });
        }
        else
        {
            batcher.quad(content_x, cursor_y, row_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.surface_hover.r,
                             theme.surface_hover.g,
                             theme.surface_hover.b,
                             theme.surface_hover.a });
        }

        // 3b. Left-edge game-mode accent bar.
        batcher.quad(content_x, cursor_y,
                     kEdgeBarW, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        // 3c. Room name label rect (occupies ~50% of row width after the edge bar).
        const float label_x = content_x + kEdgeBarW + kPad;
        const float label_w = row_w * 0.5F;
        batcher.quad(label_x, cursor_y + kRowH * 0.2F,
                     label_w, kRowH * 0.6F,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         is_selected ? static_cast<std::uint8_t>(220U) : static_cast<std::uint8_t>(160U) });

        // 3d. Player-count fill bar.
        //     Track occupies the space between the label and the lock column.
        const float lock_col_w  = kLockW + kPad;
        const float track_x     = label_x + label_w + kPad;
        const float track_w     = row_w - (label_x - content_x) - label_w - kPad - lock_col_w;

        if (track_w > 0.0F)
        {
            // Track background.
            batcher.quad(track_x, cursor_y + kRowH * 0.35F,
                         track_w, kRowH * 0.3F,
                         cd::ui::renderer::Color {
                             theme.surface.r,
                             theme.surface.g,
                             theme.surface.b,
                             180U });

            // Filled proportion.
            const auto  max_p  = static_cast<float>(rs.config.max_players);
            const auto  cur_p  = static_cast<float>(rs.players.size());
            const float norm   = (max_p > 0.0F)
                                 ? std::clamp(cur_p / max_p, 0.0F, 1.0F)
                                 : 0.0F;
            if (norm > 0.0F)
            {
                batcher.quad(track_x, cursor_y + kRowH * 0.35F,
                             track_w * norm, kRowH * 0.3F,
                             cd::ui::renderer::Color { 100U, 220U, 100U, 200U });
            }
        }

        // 3e. Lock icon (drawn only when a passcode is set).
        if (!rs.config.passcode.empty())
        {
            const float lock_x = content_x + row_w - lock_col_w;
            const float lock_y = cursor_y + (kRowH - kLockH) * 0.5F;
            batcher.quad(lock_x, lock_y,
                         kLockW, kLockH,
                         cd::ui::renderer::Color { 255U, 200U, 60U, 220U });
        }

        cursor_y += kRowH + kRowGap;
    }
}

}  // namespace cd::editor::panel::lobby_browser
