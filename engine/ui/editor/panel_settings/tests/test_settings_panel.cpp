// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_settings/tests/test_settings_panel.cpp
//
// phase698 — unit tests for cd::editor::panel::settings::SettingsPanel.
//
// All tests are headless (no RHI, no ImGui). We verify:
//
//   1. DefaultCtor
//         — starts with kGraphics active, zero entries.
//   2. RegisterEntries_FilterByCategory
//         — register 6 entries across 3 categories; entry_count() and
//           entries_in_category() return the correct counts.
//   3. SetValue_RoundTrip
//         — register an entry with key "vsync", call set_value("vsync", "off"),
//           get_value("vsync") returns "off".
//   4. MissingKey_ReturnsNullopt
//         — get_value("nonexistent") returns std::nullopt.
//   5. SetActiveCategory_ChangesActiveAndReturnsCorrect
//         — set_active_category(kAudio), active_category() == kAudio.
//   6. DrawEmitsMoreVerticesWithEntries
//         — draw() with registered entries emits more vertices than draw()
//           on an empty panel (same bounds).
//   7. SetValue_NoopForUnknownKey
//         — set_value on unknown key leaves all entries unchanged.
// =============================================================================
#include <cd/editor/panel_settings/SettingsPanel.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <string>

namespace sp = cd::editor::panel::settings;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 400.0F, 600.0F };
}

[[nodiscard]] cd::ui::widgets::Theme standard_theme() noexcept
{
    return {};
}

/// Populate a panel with entries across multiple categories.
void populate_panel(sp::SettingsPanel& panel)
{
    // Graphics (3 entries)
    panel.register_entry({ "vsync",          "V-Sync",            "on",  "Enable vertical sync.",              sp::Category::kGraphics });
    panel.register_entry({ "shadow_quality", "Shadow Quality",    "high","High: 4096px shadow maps.",           sp::Category::kGraphics });
    panel.register_entry({ "aa_mode",        "Anti-Aliasing",     "TAA", "Temporal Anti-Aliasing (default).",   sp::Category::kGraphics });
    // Input (2 entries)
    panel.register_entry({ "mouse_sens",     "Mouse Sensitivity",  "1.0", "Horizontal + vertical sensitivity.", sp::Category::kInput });
    panel.register_entry({ "invert_y",       "Invert Y Axis",      "off", "",                                   sp::Category::kInput });
    // Audio (1 entry)
    panel.register_entry({ "master_vol",     "Master Volume",      "80",  "Global volume (0-100).",             sp::Category::kAudio });
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtor
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, DefaultCtor)
{
    const sp::SettingsPanel panel;

    EXPECT_EQ(panel.active_category(), sp::Category::kGraphics);
    EXPECT_EQ(panel.entry_count(),     static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST 2 — RegisterEntries_FilterByCategory
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, RegisterEntries_FilterByCategory)
{
    sp::SettingsPanel panel;
    populate_panel(panel);

    EXPECT_EQ(panel.entry_count(), static_cast<std::size_t>(6U));

    EXPECT_EQ(panel.entries_in_category(sp::Category::kGraphics), static_cast<std::size_t>(3U));
    EXPECT_EQ(panel.entries_in_category(sp::Category::kInput),    static_cast<std::size_t>(2U));
    EXPECT_EQ(panel.entries_in_category(sp::Category::kAudio),    static_cast<std::size_t>(1U));
    EXPECT_EQ(panel.entries_in_category(sp::Category::kEditor),   static_cast<std::size_t>(0U));
    EXPECT_EQ(panel.entries_in_category(sp::Category::kAdvanced), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST 3 — SetValue_RoundTrip
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, SetValue_RoundTrip)
{
    sp::SettingsPanel panel;
    panel.register_entry({ "vsync", "V-Sync", "on", "Enable vertical sync.", sp::Category::kGraphics });

    panel.set_value("vsync", "off");

    const auto result = panel.get_value("vsync");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "off");
}

// ---------------------------------------------------------------------------
// TEST 4 — MissingKey_ReturnsNullopt
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, MissingKey_ReturnsNullopt)
{
    sp::SettingsPanel panel;
    populate_panel(panel);

    EXPECT_FALSE(panel.get_value("nonexistent_key").has_value());
}

// ---------------------------------------------------------------------------
// TEST 5 — SetActiveCategory_ChangesActive
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, SetActiveCategory_ChangesActive)
{
    sp::SettingsPanel panel;

    // Default is kGraphics.
    EXPECT_EQ(panel.active_category(), sp::Category::kGraphics);

    panel.set_active_category(sp::Category::kAudio);
    EXPECT_EQ(panel.active_category(), sp::Category::kAudio);

    panel.set_active_category(sp::Category::kAdvanced);
    EXPECT_EQ(panel.active_category(), sp::Category::kAdvanced);
}

// ---------------------------------------------------------------------------
// TEST 6 — DrawEmitsMoreVerticesWithEntries
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, DrawEmitsMoreVerticesWithEntries)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    // Empty panel — at minimum background + 5 tab quads + separator bar.
    sp::SettingsPanel empty_panel;
    cd::ui::renderer::DrawBatcher batcher_empty;
    batcher_empty.begin_frame();
    empty_panel.draw(batcher_empty, theme, bounds);
    const std::size_t verts_empty = batcher_empty.vertex_count();
    EXPECT_GE(verts_empty, static_cast<std::size_t>(4U));

    // Panel with entries in the active category — must emit more vertices.
    sp::SettingsPanel full_panel;
    populate_panel(full_panel);  // active category = kGraphics (3 entries)
    cd::ui::renderer::DrawBatcher batcher_full;
    batcher_full.begin_frame();
    full_panel.draw(batcher_full, theme, bounds);
    const std::size_t verts_full = batcher_full.vertex_count();
    EXPECT_GT(verts_full, verts_empty);
}

// ---------------------------------------------------------------------------
// TEST 7 — SetValue_NoopForUnknownKey
// ---------------------------------------------------------------------------
TEST(SettingsPanelTest, SetValue_NoopForUnknownKey)
{
    sp::SettingsPanel panel;
    populate_panel(panel);

    // Existing value before the no-op call.
    const auto before = panel.get_value("vsync");
    ASSERT_TRUE(before.has_value());

    // Write to a key that does not exist.
    panel.set_value("does_not_exist", "some_value");

    // All existing entries must be unchanged.
    const auto after = panel.get_value("vsync");
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*before, *after);

    // The non-existent key must still return nullopt.
    EXPECT_FALSE(panel.get_value("does_not_exist").has_value());

    // Entry count must not have changed.
    EXPECT_EQ(panel.entry_count(), static_cast<std::size_t>(6U));
}
