// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_keyboard_shortcut_overlay/src/KeyboardShortcutOverlay.cpp
//
// phase688 — cd::editor::panel::keyboard_shortcut_overlay implementation
// =============================================================================
#include <cd/editor/panel_keyboard_shortcut_overlay/KeyboardShortcutOverlay.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <vector>

namespace cd::editor::panel::keyboard_shortcut_overlay
{

// ---------------------------------------------------------------------------
// Registration API
// ---------------------------------------------------------------------------

void KeyboardShortcutOverlay::register_shortcut(const Shortcut& shortcut)
{
    shortcuts_.push_back(shortcut);
}

std::size_t KeyboardShortcutOverlay::shortcut_count() const noexcept
{
    return shortcuts_.size();
}

// ---------------------------------------------------------------------------
// Visibility API
// ---------------------------------------------------------------------------

void KeyboardShortcutOverlay::set_visible(bool visible) noexcept
{
    visible_ = visible;
}

bool KeyboardShortcutOverlay::is_visible() const noexcept
{
    return visible_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

namespace
{
// Collect unique category names in first-seen insertion order.
[[nodiscard]] std::vector<std::string>
collect_categories(const std::vector<Shortcut>& shortcuts)
{
    std::vector<std::string> cats;
    for (const auto& s : shortcuts)
    {
        const bool already_seen =
            std::ranges::any_of(cats,
                        [&](const std::string& c) { return c == s.category; });
        if (!already_seen)
            cats.push_back(s.category);
    }
    return cats;
}
}  // anonymous namespace

void KeyboardShortcutOverlay::draw(cd::ui::renderer::DrawBatcher& batcher,
                                   const cd::ui::widgets::Theme&  theme,
                                   const cd::ui::widgets::Rect&   bounds) const
{
    if (!visible_)
        return;

    // -------------------------------------------------------------------------
    // 1. Full-screen semi-transparent dim backdrop (ignores bounds size).
    //    We use `bounds` as the full-screen rect supplied by the caller, which
    //    is conventionally the framebuffer dimensions.
    // -------------------------------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.dim_overlay.r,
                     theme.dim_overlay.g,
                     theme.dim_overlay.b,
                     200U });

    if (shortcuts_.empty())
        return;

    // -------------------------------------------------------------------------
    // 2. Centred content panel background.
    //    Panel takes up ~70% of width and 80% of height, centred.
    // -------------------------------------------------------------------------
    constexpr float kPanelWFrac = 0.70F;
    constexpr float kPanelHFrac = 0.80F;
    constexpr float kPad        = 12.0F;
    constexpr float kHeaderH    = 6.0F;
    constexpr float kCatHeaderH = 16.0F;
    constexpr float kRowH       = 18.0F;
    constexpr float kKeyPillW   = 80.0F;
    constexpr float kRowGap     = 2.0F;
    constexpr float kColGap     = 20.0F;
    constexpr float kCatGap     = 6.0F;

    const float panel_w = bounds.w * kPanelWFrac;
    const float panel_h = bounds.h * kPanelHFrac;
    const float panel_x = bounds.x + (bounds.w - panel_w) * 0.5F;
    const float panel_y = bounds.y + (bounds.h - panel_h) * 0.5F;

    // Panel background.
    batcher.quad(panel_x, panel_y, panel_w, panel_h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     245U });

    // -------------------------------------------------------------------------
    // 3. Accent header bar at the top of the panel ("Keyboard Shortcuts").
    // -------------------------------------------------------------------------
    batcher.quad(panel_x, panel_y, panel_w, kHeaderH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // -------------------------------------------------------------------------
    // 4. Category columns — one column per unique category, left→right.
    //    Each column: category header quad + one key-pill + label row per entry.
    // -------------------------------------------------------------------------
    const std::vector<std::string> categories = collect_categories(shortcuts_);

    // Compute column width from available space and number of categories.
    const std::size_t num_cats = categories.size();
    const float avail_w  = panel_w - 2.0F * kPad;
    const float col_w    = num_cats > 0U
                               ? (avail_w - static_cast<float>(num_cats - 1U) * kColGap)
                                     / static_cast<float>(num_cats)
                               : avail_w;

    float col_x = panel_x + kPad;
    const float content_top = panel_y + kHeaderH + kPad;

    for (const auto& cat : categories)
    {
        // Category header background strip.
        batcher.quad(col_x, content_top, col_w, kCatHeaderH,
                     cd::ui::renderer::Color {
                         theme.accent_warning.r,
                         theme.accent_warning.g,
                         theme.accent_warning.b,
                         static_cast<std::uint8_t>(60U) });

        // Accent left border of category header.
        batcher.quad(col_x, content_top, 3.0F, kCatHeaderH,
                     cd::ui::renderer::Color {
                         theme.accent_warning.r,
                         theme.accent_warning.g,
                         theme.accent_warning.b,
                         theme.accent_warning.a });

        float row_y = content_top + kCatHeaderH + kCatGap;

        // Shortcut rows within this category.
        for (const auto& shortcut : shortcuts_)
        {
            if (shortcut.category != cat)
                continue;

            // Guard: stop if we exceed panel bottom.
            if (row_y + kRowH > panel_y + panel_h - kPad)
                break;

            // Key-pill background (accent colour).
            batcher.quad(col_x, row_y, kKeyPillW, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             static_cast<std::uint8_t>(80U) });

            // Key-pill accent left border.
            batcher.quad(col_x, row_y, 2.0F, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             theme.accent.a });

            // Action description label background (dimmed text slot).
            const float label_x = col_x + kKeyPillW + kRowGap;
            const float label_w = col_w - kKeyPillW - kRowGap;
            if (label_w > 0.0F)
            {
                batcher.quad(label_x, row_y, label_w, kRowH,
                             cd::ui::renderer::Color {
                                 theme.surface_hover.r,
                                 theme.surface_hover.g,
                                 theme.surface_hover.b,
                                 static_cast<std::uint8_t>(120U) });
            }

            row_y += kRowH + kRowGap;
        }

        col_x += col_w + kColGap;
    }
}

}  // namespace cd::editor::panel::keyboard_shortcut_overlay
