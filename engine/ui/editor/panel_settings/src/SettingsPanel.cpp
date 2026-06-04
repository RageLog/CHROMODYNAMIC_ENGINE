// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_settings/src/SettingsPanel.cpp
//
// phase698 — cd::editor::panel::settings  implementation
// =============================================================================
#include <cd/editor/panel_settings/SettingsPanel.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::editor::panel::settings
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace
{

/// Human-readable tab label for each category.
[[nodiscard]] const char* category_label(Category cat) noexcept
{
    switch (cat)
    {
    case Category::kGraphics: return "Graphics";
    case Category::kInput:    return "Input";
    case Category::kAudio:    return "Audio";
    case Category::kEditor:   return "Editor";
    case Category::kAdvanced: return "Advanced";
    }
    return "Unknown";
}

/// Compile-time ordered list of categories for iteration.
constexpr std::array<Category, 5U> kAllCategories {
    Category::kGraphics,
    Category::kInput,
    Category::kAudio,
    Category::kEditor,
    Category::kAdvanced,
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Entry registry
// ---------------------------------------------------------------------------

void SettingsPanel::register_entry(const Entry& entry)
{
    entries_.push_back(entry);
}

void SettingsPanel::set_value(std::string_view key, std::string value)
{
    for (Entry& e : entries_)
    {
        if (e.key == key)
        {
            e.value = std::move(value);
            return;
        }
    }
    // Key not found — no-op.
}

std::optional<std::string> SettingsPanel::get_value(std::string_view key) const
{
    for (const Entry& e : entries_)
    {
        if (e.key == key)
            return e.value;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Tab / category API
// ---------------------------------------------------------------------------

void SettingsPanel::set_active_category(Category category) noexcept
{
    active_category_ = category;
}

Category SettingsPanel::active_category() const noexcept
{
    return active_category_;
}

// ---------------------------------------------------------------------------
// Counts
// ---------------------------------------------------------------------------

std::size_t SettingsPanel::entry_count() const noexcept
{
    return entries_.size();
}

std::size_t SettingsPanel::entries_in_category(Category category) const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(),
            [category](const Entry& e) { return e.category == category; }));
}

// ---------------------------------------------------------------------------
// category_swatch_color — private static
// ---------------------------------------------------------------------------

cd::ui::renderer::Color SettingsPanel::category_swatch_color(Category cat) noexcept
{
    // One distinct accent per category so rows carry a visual category hint.
    switch (cat)
    {
    case Category::kGraphics: return {  96U, 160U, 255U, 220U };  // blue
    case Category::kInput:    return {  80U, 200U, 100U, 220U };  // green
    case Category::kAudio:    return { 220U, 200U,  80U, 220U };  // yellow
    case Category::kEditor:   return { 180U,  80U, 220U, 220U };  // purple
    case Category::kAdvanced: return { 200U,  80U,  80U, 220U };  // red
    }
    return { 160U, 160U, 160U, 220U };
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void SettingsPanel::draw(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background ------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    const float inner_w   = bounds.w - 2.0F * kPad;
    const float tab_w     = inner_w / static_cast<float>(kCategoryCount);

    // ---- 2. Tab strip — 5 tabs ----------------------------------------------
    for (std::size_t ti = 0U; ti < kCategoryCount; ++ti)
    {
        const auto  cat      = kAllCategories[ti];
        const bool  is_active = (cat == active_category_);
        const float tab_x    = bounds.x + kPad + static_cast<float>(ti) * tab_w;
        const float tab_y    = bounds.y + kPad;

        // Tab background: accent for active, surface_hover for inactive.
        if (is_active)
        {
            batcher.quad(tab_x, tab_y, tab_w - 1.0F, kTabH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             theme.accent.a });
        }
        else
        {
            batcher.quad(tab_x, tab_y, tab_w - 1.0F, kTabH,
                         cd::ui::renderer::Color {
                             theme.surface_hover.r,
                             theme.surface_hover.g,
                             theme.surface_hover.b,
                             theme.surface_hover.a });
        }
        // Suppress unused-variable warning for the category label in non-text
        // draw path; the label is available via category_label(cat) for text
        // renderers in Sprint-2.
        (void)category_label(cat);
    }

    // ---- 3. Accent separator bar below tab strip ---------------------------
    const float bar_y = bounds.y + kPad + kTabH + kPad;
    batcher.quad(bounds.x + kPad, bar_y,
                 inner_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // ---- 4. Entry rows for the active category -----------------------------
    std::size_t row_idx = 0U;
    for (const Entry& e : entries_)
    {
        if (e.category != active_category_)
            continue;

        const float ry = bounds.y + row_top(row_idx);

        // Clip rows that overflow the panel bottom.
        if (ry + kRowH > bounds.y + bounds.h - kPad)
            break;

        // Row background — alternating shade.
        const bool        even  = (row_idx % 2U == 0U);
        const auto        bg    = even ? theme.surface : theme.surface_hover;
        batcher.quad(bounds.x + kPad, ry,
                     inner_w, kRowH,
                     cd::ui::renderer::Color { bg.r, bg.g, bg.b, bg.a });

        // Left-edge category colour swatch.
        batcher.quad(bounds.x + kPad, ry + 2.0F,
                     kSwatchW, kRowH - 4.0F,
                     category_swatch_color(e.category));

        // Value field — right-side portion of the row.
        const float value_w = inner_w * kValueRatio;
        const float value_x = bounds.x + kPad + inner_w - value_w;
        batcher.quad(value_x, ry + 2.0F,
                     value_w - 2.0F, kRowH - 4.0F,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         200U });

        // Tooltip focus ring: a thin accent border around the row when a
        // tooltip is registered. Sprint-2 will track the "focused" row;
        // for Sprint-1 we draw the ring whenever the tooltip is non-empty.
        if (!e.tooltip.empty())
        {
            // Top edge.
            batcher.quad(bounds.x + kPad, ry,
                         inner_w, 1.0F,
                         cd::ui::renderer::Color {
                             theme.accent.r, theme.accent.g,
                             theme.accent.b, 80U });
            // Bottom edge.
            batcher.quad(bounds.x + kPad, ry + kRowH - 1.0F,
                         inner_w, 1.0F,
                         cd::ui::renderer::Color {
                             theme.accent.r, theme.accent.g,
                             theme.accent.b, 80U });
        }

        ++row_idx;
    }
}

}  // namespace cd::editor::panel::settings
