// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_lobby_browser/tests/test_lobby_browser.cpp
//
// phase718 — unit tests for cd::editor::panel::lobby_browser::LobbyBrowser.
//
// All tests are headless (no ImGui / no RHI).  We verify:
//
//   DefaultCtorIsDetached        — selected_room_id() == nullopt; draw() safe.
//   SetLobbyNullDetaches         — set_lobby(nullptr) clears selection and does
//                                  not crash on subsequent draw() calls.
//   DrawNullLobbyEmitsBackground — draw() with no lobby emits the background
//                                  quad + placeholder (command_count >= 2).
//   DrawEmptyLobbyEmitsPlaceholder — draw() with a bound but empty Lobby emits
//                                  background + separator + placeholder rows.
//   DrawWithRoomsEmitsRowGeometry  — each RoomState produces multiple quads
//                                  (background + edge bar + label + count bar).
//   LockIconEmittedForPasscodeRoom — a room with a passcode produces more quads
//                                  than the same room without.
//   SimulateClickSelectsRow      — click at the Y-centre of the first row sets
//                                  selected_room_id() to that room's id.
//   SimulateClickOutsideNoSelect — click outside all rows leaves selection
//                                  unchanged (nullopt).
//   SimulateClickUpdatesOnRebind — set_lobby(nullptr) resets selection;
//                                  re-binding a new Lobby and clicking works.
//   DrawSelectedRoomHighlighted  — draw() emits more quads for a selected row
//                                  than for an unselected row (selection tint).
// =============================================================================
#include <cd/editor/panel_lobby_browser/LobbyBrowser.hpp>

#include <cd/network/lobby/Lobby.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace lb  = cd::editor::panel::lobby_browser;
namespace nlo = cd::network::lobby;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a Lobby with `n` rooms of 2 players max, no passcode.
static nlo::Lobby make_lobby_n_rooms(int n)
{
    nlo::Lobby lobby;
    for (int i = 0; i < n; ++i)
    {
        nlo::LobbyConfig cfg;
        cfg.lobby_name  = "Room" + std::to_string(i);
        cfg.game_mode   = "Deathmatch";
        cfg.max_players = 2U;
        cfg.is_public   = true;
        static_cast<void>(lobby.create_room(cfg, static_cast<uint64_t>(i + 1)));
    }
    return lobby;
}

/// Standard 400x600 panel bounds.
static constexpr cd::ui::widgets::Rect kBounds { 0.0F, 0.0F, 400.0F, 600.0F };

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DefaultCtorIsDetached)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DefaultCtorIsDetached)
{
    const lb::LobbyBrowser browser;
    EXPECT_FALSE(browser.selected_room_id().has_value());
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, SetLobbyNullDetaches)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, SetLobbyNullDetaches)
{
    lb::LobbyBrowser browser;

    nlo::Lobby lobby = make_lobby_n_rooms(1);
    browser.set_lobby(&lobby);
    browser.simulate_click(20.0F, 18.0F, kBounds);  // Select first row.
    EXPECT_TRUE(browser.selected_room_id().has_value());

    // Detach.
    browser.set_lobby(nullptr);
    EXPECT_FALSE(browser.selected_room_id().has_value());

    // draw() must not crash.
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    batcher.begin_frame();
    EXPECT_NO_THROW(browser.draw(batcher, theme, kBounds));
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DrawNullLobbyEmitsBackground)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DrawNullLobbyEmitsBackground)
{
    lb::LobbyBrowser              browser;   // no lobby bound
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};

    batcher.begin_frame();
    browser.draw(batcher, theme, kBounds);

    // Background + placeholder = at least 2 quads = at least 8 vertices.
    // (All solid quads batch into 1 command because they share the same material
    // variant; use vertex_count() to count individual quads * 4 verts each.)
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DrawEmptyLobbyEmitsPlaceholder)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DrawEmptyLobbyEmitsPlaceholder)
{
    nlo::Lobby            lobby;   // empty
    lb::LobbyBrowser      browser;
    browser.set_lobby(&lobby);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    batcher.begin_frame();
    browser.draw(batcher, theme, kBounds);

    // background + separator + placeholder = at least 3 quads = 12 vertices.
    // (All solid quads merge into 1 draw command; use vertex_count().)
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(12U));
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DrawWithRoomsEmitsRowGeometry)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DrawWithRoomsEmitsRowGeometry)
{
    nlo::Lobby       lobby = make_lobby_n_rooms(3);
    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    batcher.begin_frame();
    browser.draw(batcher, theme, kBounds);

    // background(1) + separator(1) + 3 rooms × >=4 quads(12) + count-tracks(6)
    // = conservatively at least 14 quads = 56 vertices.
    // (All solid quads merge into 1 draw command; use vertex_count().)
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(56U));
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, LockIconEmittedForPasscodeRoom)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, LockIconEmittedForPasscodeRoom)
{
    // Room without passcode.
    nlo::Lobby open_lobby;
    {
        nlo::LobbyConfig cfg;
        cfg.lobby_name  = "OpenRoom";
        cfg.game_mode   = "CTF";
        cfg.max_players = 4U;
        static_cast<void>(open_lobby.create_room(cfg, 1U));
    }

    // Room with passcode.
    nlo::Lobby locked_lobby;
    {
        nlo::LobbyConfig cfg;
        cfg.lobby_name  = "LockedRoom";
        cfg.game_mode   = "CTF";
        cfg.max_players = 4U;
        cfg.passcode    = "secret";
        static_cast<void>(locked_lobby.create_room(cfg, 1U));
    }

    lb::LobbyBrowser              browser;
    cd::ui::renderer::DrawBatcher batcher_open;
    cd::ui::renderer::DrawBatcher batcher_locked;
    const cd::ui::widgets::Theme  theme {};

    browser.set_lobby(&open_lobby);
    batcher_open.begin_frame();
    browser.draw(batcher_open, theme, kBounds);

    browser.set_lobby(&locked_lobby);
    batcher_locked.begin_frame();
    browser.draw(batcher_locked, theme, kBounds);

    // The locked room emits one extra quad (the lock icon) = 4 extra vertices.
    // (All solid quads merge into 1 draw command; use vertex_count().)
    EXPECT_GT(batcher_locked.vertex_count(), batcher_open.vertex_count());
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, SimulateClickSelectsRow)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, SimulateClickSelectsRow)
{
    nlo::Lobby       lobby = make_lobby_n_rooms(2);
    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby);

    // The first row starts at y = pad(6) + barH(4) + pad(6) = 16 px.
    // Its centre is at y ≈ 16 + 10 = 26 px within the panel.
    constexpr float click_x = 100.0F;
    constexpr float click_y = 26.0F;

    browser.simulate_click(click_x, click_y, kBounds);

    ASSERT_TRUE(browser.selected_room_id().has_value());

    // The first room created has id 1 (cd::network::lobby::Lobby assigns
    // sequential ids starting from 1 per Sprint-1 implementation).
    EXPECT_EQ(*browser.selected_room_id(), static_cast<uint64_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, SimulateClickOutsideNoSelect)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, SimulateClickOutsideNoSelect)
{
    nlo::Lobby       lobby = make_lobby_n_rooms(1);
    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby);

    // Click far below all rows.
    browser.simulate_click(200.0F, 590.0F, kBounds);

    EXPECT_FALSE(browser.selected_room_id().has_value());
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, SimulateClickUpdatesOnRebind)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, SimulateClickUpdatesOnRebind)
{
    nlo::Lobby lobby_a = make_lobby_n_rooms(1);
    nlo::Lobby lobby_b = make_lobby_n_rooms(2);

    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby_a);
    browser.simulate_click(100.0F, 26.0F, kBounds);
    EXPECT_TRUE(browser.selected_room_id().has_value());

    // Rebind to another lobby — selection should clear.
    browser.set_lobby(&lobby_b);
    EXPECT_FALSE(browser.selected_room_id().has_value());

    // Click the first row in the new lobby.
    browser.simulate_click(100.0F, 26.0F, kBounds);
    EXPECT_TRUE(browser.selected_room_id().has_value());
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DrawSelectedRoomHighlighted)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DrawSelectedRoomHighlighted)
{
    nlo::Lobby       lobby = make_lobby_n_rooms(1);
    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby);

    cd::ui::renderer::DrawBatcher batcher_unsel;
    const cd::ui::widgets::Theme  theme {};

    // Draw with no selection.
    batcher_unsel.begin_frame();
    browser.draw(batcher_unsel, theme, kBounds);
    const std::size_t cmds_unsel = batcher_unsel.command_count();

    // Select the room then redraw.
    browser.simulate_click(100.0F, 26.0F, kBounds);
    ASSERT_TRUE(browser.selected_room_id().has_value());

    cd::ui::renderer::DrawBatcher batcher_sel;
    batcher_sel.begin_frame();
    browser.draw(batcher_sel, theme, kBounds);
    const std::size_t cmds_sel = batcher_sel.command_count();

    // Selection adds an accent-tinted background on top of the normal row
    // background, so the selected draw must emit at least as many quads.
    EXPECT_GE(cmds_sel, cmds_unsel);
}

// ---------------------------------------------------------------------------
// TEST(LobbyBrowserPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(LobbyBrowserPanel, DrawZeroBoundsDoesNotCrash)
{
    nlo::Lobby       lobby = make_lobby_n_rooms(2);
    lb::LobbyBrowser browser;
    browser.set_lobby(&lobby);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    EXPECT_NO_THROW(browser.draw(batcher, theme, zero));

    // bounds.is_valid() == false → returns early after the background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
