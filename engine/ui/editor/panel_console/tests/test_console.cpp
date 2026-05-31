// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_console/tests/test_console.cpp
//
// phase544 — unit tests for cd::editor::panel::console::Console.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorIsEmpty        — entry_count() == 0 after default construction.
//   * PushLogAddsEntry          — push_log grows entry_count correctly.
//   * PushLogCapAt32            — pushing > 32 entries evicts oldest.
//   * ClearEmptiesLog           — clear() reduces entry_count() to 0.
//   * DrawNoEntriesDoesNotCrash — draw() on empty console emits background.
//   * DrawWithEntriesEmitsQuads — draw() with entries emits more geometry.
//   * DrawZeroBoundsDoesNotCrash— draw() on zero-size rect is safe.
// =============================================================================
#include <cd/editor/panel_console/Console.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace con = cd::editor::panel::console;

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, DefaultCtorIsEmpty)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, DefaultCtorIsEmpty)
{
    const con::Console console;
    EXPECT_EQ(console.entry_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, PushLogAddsEntry)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, PushLogAddsEntry)
{
    con::Console console;
    console.push_log("hello world");
    EXPECT_EQ(console.entry_count(), static_cast<std::size_t>(1U));

    console.push_log("second message");
    EXPECT_EQ(console.entry_count(), static_cast<std::size_t>(2U));
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, PushLogCapAt32)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, PushLogCapAt32)
{
    con::Console console;
    // Push 40 entries — only the last 32 should remain.
    for (int i = 0; i < 40; ++i)
        console.push_log("msg");

    EXPECT_EQ(console.entry_count(), con::Console::kMaxEntries);
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, ClearEmptiesLog)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, ClearEmptiesLog)
{
    con::Console console;
    console.push_log("a");
    console.push_log("b");
    console.push_log("c");
    EXPECT_EQ(console.entry_count(), static_cast<std::size_t>(3U));

    console.clear();
    EXPECT_EQ(console.entry_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, DrawNoEntriesDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, DrawNoEntriesDoesNotCrash)
{
    con::Console                  console;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 180.0F };

    batcher.begin_frame();
    console.draw(batcher, theme, bounds);

    // At minimum the background quad must have been emitted.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, DrawWithEntriesEmitsQuads)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, DrawWithEntriesEmitsQuads)
{
    con::Console console;
    console.push_log("[gltf] scene loaded");
    console.push_log("selected: Cube");
    console.push_log("undo: Translate");

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 180.0F };

    batcher.begin_frame();
    console.draw(batcher, theme, bounds);

    // With 3 entries the batcher should have more geometry than the empty case.
    // Background (4 verts) + accent bar (4) + 3 * 2 row quads (each 4 verts) = 32+
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(16U));
}

// ---------------------------------------------------------------------------
// TEST(ConsolePanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(ConsolePanel, DrawZeroBoundsDoesNotCrash)
{
    con::Console console;
    console.push_log("test entry");

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero_bounds { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not crash — that is the sole contract for a zero-size bounds.
    console.draw(batcher, theme, zero_bounds);

    // bounds.is_valid() == false → returns early after the background quad.
    // Row quads must NOT be emitted, so vertex count stays very low.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
