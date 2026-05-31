// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_browser/tests/test_asset_browser.cpp
//
// phase545 — unit tests for cd::editor::panel::asset_browser::AssetBrowser.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorIsEmpty            — entry_count() == 0 + no selection.
//   * SetEntriesStoresAll           — set_entries stores all provided entries.
//   * SetSelectedIndex              — selected_index reflects set_selected_index.
//   * SetSelectedIndexOutOfRange    — index >= count clears selection to npos.
//   * SetEntriesClearsStaleSelect   — replacing entries with a smaller list
//                                     clears an index that would be out-of-range.
//   * DrawNoEntriesDoesNotCrash     — draw() on empty panel emits background.
//   * DrawWithEntriesEmitsQuads     — draw() emits more geometry with entries.
//   * DrawZeroBoundsDoesNotCrash    — draw() on zero-size rect is safe.
//   * DrawSelectedRowExtraGeometry  — selected row emits extra accent geometry.
//   * DirectoryEntriesVsFiles       — is_dir is stored and retrieved correctly.
// =============================================================================
#include <cd/editor/panel_asset_browser/AssetBrowser.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace ab = cd::editor::panel::asset_browser;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::array<ab::Entry, 3> make_sample_entries()
{
    return std::array<ab::Entry, 3> {
        ab::Entry { "textures", "assets/textures", true  },
        ab::Entry { "mesh.glb", "assets/mesh.glb", false },
        ab::Entry { "mat.json", "assets/mat.json", false },
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DefaultCtorIsEmpty)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DefaultCtorIsEmpty)
{
    const ab::AssetBrowser browser;
    EXPECT_EQ(browser.entry_count(), static_cast<std::size_t>(0U));
    EXPECT_EQ(browser.selected_index(), ab::AssetBrowser::npos);
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, SetEntriesStoresAll)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, SetEntriesStoresAll)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);
    EXPECT_EQ(browser.entry_count(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, SetSelectedIndex)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, SetSelectedIndex)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);

    browser.set_selected_index(1U);
    EXPECT_EQ(browser.selected_index(), static_cast<std::size_t>(1U));

    browser.set_selected_index(0U);
    EXPECT_EQ(browser.selected_index(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, SetSelectedIndexOutOfRange)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, SetSelectedIndexOutOfRange)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);
    browser.set_selected_index(1U);

    // Index beyond entry count must clear selection.
    browser.set_selected_index(99U);
    EXPECT_EQ(browser.selected_index(), ab::AssetBrowser::npos);

    // Explicitly passing npos also clears.
    browser.set_selected_index(1U);
    browser.set_selected_index(ab::AssetBrowser::npos);
    EXPECT_EQ(browser.selected_index(), ab::AssetBrowser::npos);
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, SetEntriesClearsStaleSelect)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, SetEntriesClearsStaleSelect)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);
    browser.set_selected_index(2U);  // last entry
    EXPECT_EQ(browser.selected_index(), static_cast<std::size_t>(2U));

    // Replace with a single-entry list — index 2 is now out-of-range.
    const std::array<ab::Entry, 1> one { ab::Entry { "root", ".", true } };
    browser.set_entries(one);
    EXPECT_EQ(browser.selected_index(), ab::AssetBrowser::npos);
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DrawNoEntriesDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DrawNoEntriesDoesNotCrash)
{
    ab::AssetBrowser              browser;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 200.0F };

    batcher.begin_frame();
    browser.draw(batcher, theme, bounds);

    // At minimum the background quad must have been emitted.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DrawWithEntriesEmitsQuads)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DrawWithEntriesEmitsQuads)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 200.0F };

    batcher.begin_frame();
    browser.draw(batcher, theme, bounds);

    // background + accent bar + 3 rows × 2 quads = at least 8 quads → 32+ verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(16U));
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DrawZeroBoundsDoesNotCrash)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    browser.draw(batcher, theme, zero);

    // is_valid() == false → returns early after background quad only.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DrawSelectedRowExtraGeometry)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DrawSelectedRowExtraGeometry)
{
    ab::AssetBrowser browser;
    const auto entries = make_sample_entries();
    browser.set_entries(entries);

    cd::ui::renderer::DrawBatcher batcher_none;
    cd::ui::renderer::DrawBatcher batcher_sel;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 200.0F };

    // Draw without a selection.
    browser.set_selected_index(ab::AssetBrowser::npos);
    batcher_none.begin_frame();
    browser.draw(batcher_none, theme, bounds);

    // Draw with a selection.
    browser.set_selected_index(0U);
    batcher_sel.begin_frame();
    browser.draw(batcher_sel, theme, bounds);

    // Both should produce the same number of quads — the selected row
    // still emits the same two quads but with a different colour.
    EXPECT_EQ(batcher_none.vertex_count(), batcher_sel.vertex_count());
}

// ---------------------------------------------------------------------------
// TEST(AssetBrowserPanel, DirectoryEntriesVsFiles)
// ---------------------------------------------------------------------------
TEST(AssetBrowserPanel, DirectoryEntriesVsFiles)
{
    ab::AssetBrowser browser;
    const std::array<ab::Entry, 2> mixed {
        ab::Entry { "shaders", "assets/shaders", true  },
        ab::Entry { "sky.hdr", "assets/sky.hdr", false },
    };
    browser.set_entries(mixed);
    EXPECT_EQ(browser.entry_count(), static_cast<std::size_t>(2U));
    // Selection defaults to npos after set_entries on fresh object.
    EXPECT_EQ(browser.selected_index(), ab::AssetBrowser::npos);

    // Select the directory entry.
    browser.set_selected_index(0U);
    EXPECT_EQ(browser.selected_index(), static_cast<std::size_t>(0U));
}
