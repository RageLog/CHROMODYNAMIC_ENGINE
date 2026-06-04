// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_scene_navigator/tests/test_scene_navigator.cpp
//
// phase687 — unit tests for cd::editor::panel::scene_navigator::SceneNavigator.
//
// All tests are headless (no RHI, no ImGui). We verify:
//
//   1. DefaultCtor
//         — starts with no selection, empty filtered list.
//   2. SetEntities_AllVisible_WithEmptyFilter
//         — set_entities with 5 entities + empty filter → all 5 filtered.
//   3. FilterNarrowsResults
//         — set_filter("enem") on a mixed list → only matching entries pass.
//   4. ClearFilterShowsAll
//         — set_filter("") after filtering → filtered_indices().size() == total.
//   5. CaseInsensitiveMatch
//         — filter "ENEM" matches "Enemy_Spawner_01" (lower-case comparison).
//   6. SimulateClickSelectsFilteredRow
//         — simulate_click lands on filtered row 2 → correct entity selected.
//   7. SimulateTextInputAppendsToFilter
//         — simulate_text_input appends characters and narrows list.
//   8. DrawEmitsCommandsProportionalToEntityCount
//         — draw() with N entities emits strictly more vertices than draw()
//           with 0 entities (for the same panel bounds).
//   9. SelectionPreservedAfterFilterChange
//         — selecting an entity, then changing the filter, keeps selected_entity().
//  10. SelectionClearedWhenEntityRemovedFromList
//         — set_entities with a list that does not include the selected entity
//           clears the selection.
// =============================================================================
#include <cd/editor/panel_scene_navigator/SceneNavigator.hpp>

#include <cd/ecs/Entity.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace sn = cd::editor::panel::scene_navigator;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

/// Build a simple valid Entity with given id and generation 1.
[[nodiscard]] cd::ecs::Entity make_entity(std::uint32_t id) noexcept
{
    return cd::ecs::Entity { id, 1U };
}

/// Returns a standard 320x600 bounds rectangle.
[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 320.0F, 600.0F };
}

/// Returns a default-constructed theme.
[[nodiscard]] cd::ui::widgets::Theme standard_theme() noexcept
{
    return {};
}

/// Populate a navigator with a small mixed-name entity list.
void populate_mixed(sn::SceneNavigator& nav)
{
    // 6 entities: 2 enemies, 2 props, 1 player, 1 enemy_boss
    const std::vector<cd::ecs::Entity> ents {
        make_entity(1U),  // "Enemy_Spawner_01"
        make_entity(2U),  // "Prop_Crate_A"
        make_entity(3U),  // "Enemy_Spawner_02"
        make_entity(4U),  // "Player_Start"
        make_entity(5U),  // "Prop_Barrel_03"
        make_entity(6U),  // "Enemy_Boss_Chamber"
    };
    const std::vector<std::string> names {
        "Enemy_Spawner_01",
        "Prop_Crate_A",
        "Enemy_Spawner_02",
        "Player_Start",
        "Prop_Barrel_03",
        "Enemy_Boss_Chamber",
    };
    nav.set_entities(ents, names);
}

// Row geometry constants (must match SceneNavigator internals).
constexpr float kPad       =  6.0F;
constexpr float kSearchH   = 20.0F;
constexpr float kBarH      =  4.0F;
constexpr float kListOffsetY = kPad + kSearchH + kPad + kBarH + kPad;
constexpr float kRowH      = 20.0F;
constexpr float kRowGap    =  2.0F;

[[nodiscard]] float row_click_y(std::size_t filtered_idx) noexcept
{
    return kListOffsetY + static_cast<float>(filtered_idx) * (kRowH + kRowGap)
           + kRowH * 0.5F;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtor
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, DefaultCtor)
{
    const sn::SceneNavigator nav;

    EXPECT_FALSE(nav.selected_entity().has_value());
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST 2 — SetEntities_AllVisible_WithEmptyFilter
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, SetEntitiesAllVisibleWithEmptyFilter)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    // Empty filter → all 6 entities visible.
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(6U));
    EXPECT_FALSE(nav.selected_entity().has_value());
}

// ---------------------------------------------------------------------------
// TEST 3 — FilterNarrowsResults
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, FilterNarrowsResults)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    nav.set_filter("enem");

    // 3 entities match: Enemy_Spawner_01, Enemy_Spawner_02, Enemy_Boss_Chamber.
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST 4 — ClearFilterShowsAll
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, ClearFilterShowsAll)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    nav.set_filter("enem");
    ASSERT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(3U));

    nav.set_filter("");
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(6U));
}

// ---------------------------------------------------------------------------
// TEST 5 — CaseInsensitiveMatch
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, CaseInsensitiveMatch)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    // All-caps filter should match same items as lower-case.
    nav.set_filter("ENEM");
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(3U));

    nav.set_filter("EnEm");
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST 6 — SimulateClickSelectsFilteredRow
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, SimulateClickSelectsFilteredRow)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    nav.set_filter("enem");
    // Filtered indices → {0, 2, 5} (Enemy_Spawner_01, Enemy_Spawner_02, Enemy_Boss_Chamber)

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Click on filtered row 1 → should select entity at original index 2
    // (Enemy_Spawner_02).
    const float click_y = row_click_y(1U);
    nav.simulate_click(50.0F, click_y, bounds);

    ASSERT_TRUE(nav.selected_entity().has_value());
    // The entity at filtered row 1 must be Enemy_Spawner_02 (id=3).
    EXPECT_EQ(nav.selected_entity()->id, 3U);
}

// ---------------------------------------------------------------------------
// TEST 7 — SimulateTextInputAppendsToFilter
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, SimulateTextInputAppendsToFilter)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    // Type 'e', 'n', 'e', 'm' one at a time.
    nav.simulate_text_input("e");
    nav.simulate_text_input("n");
    nav.simulate_text_input("e");
    nav.simulate_text_input("m");

    // Should now match the same 3 entities as set_filter("enem").
    EXPECT_EQ(nav.filtered_indices().size(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawEmitsCommandsProportionalToEntityCount
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, DrawEmitsCommandsProportionalToEntityCount)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    // Empty navigator — at minimum background + search-box + separator quads.
    sn::SceneNavigator empty_nav;
    cd::ui::renderer::DrawBatcher batcher_empty;
    batcher_empty.begin_frame();
    empty_nav.draw(batcher_empty, theme, bounds);
    const std::size_t verts_empty = batcher_empty.vertex_count();
    EXPECT_GE(verts_empty, static_cast<std::size_t>(4U));

    // Navigator with 6 entities — must emit more vertices than empty.
    sn::SceneNavigator full_nav;
    populate_mixed(full_nav);
    cd::ui::renderer::DrawBatcher batcher_full;
    batcher_full.begin_frame();
    full_nav.draw(batcher_full, theme, bounds);
    const std::size_t verts_full = batcher_full.vertex_count();
    EXPECT_GT(verts_full, verts_empty);
}

// ---------------------------------------------------------------------------
// TEST 9 — SelectionPreservedAfterFilterChange
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, SelectionPreservedAfterFilterChange)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    // Select Enemy_Spawner_01 (id=1) via click with no filter.
    nav.set_filter("");
    const cd::ui::widgets::Rect bounds = standard_bounds();
    const float click_y = row_click_y(0U);  // First row (Enemy_Spawner_01).
    nav.simulate_click(10.0F, click_y, bounds);

    ASSERT_TRUE(nav.selected_entity().has_value());
    const cd::ecs::Entity sel = *nav.selected_entity();

    // Change filter — selection should survive.
    nav.set_filter("prop");
    ASSERT_TRUE(nav.selected_entity().has_value());
    EXPECT_EQ(*nav.selected_entity(), sel);
}

// ---------------------------------------------------------------------------
// TEST 10 — SelectionClearedWhenEntityRemovedFromList
// ---------------------------------------------------------------------------
TEST(SceneNavigatorPanel, SelectionClearedWhenEntityRemovedFromList)
{
    sn::SceneNavigator nav;
    populate_mixed(nav);

    // Select entity id=1.
    nav.set_filter("");
    const cd::ui::widgets::Rect bounds = standard_bounds();
    nav.simulate_click(10.0F, row_click_y(0U), bounds);
    ASSERT_TRUE(nav.selected_entity().has_value());
    EXPECT_EQ(nav.selected_entity()->id, 1U);

    // Replace entity list with a set that does NOT include id=1.
    const std::vector<cd::ecs::Entity> new_ents { make_entity(7U), make_entity(8U) };
    const std::vector<std::string>     new_names { "NewEntity_A", "NewEntity_B" };
    nav.set_entities(new_ents, new_names);

    EXPECT_FALSE(nav.selected_entity().has_value());
}
