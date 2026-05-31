// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_drop_target/tests/test_asset_drop_target.cpp
//
// phase594 — unit tests for cd::editor::panel::asset_drop_target::AssetDropTarget.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorIsEmpty              — no pending drop, no accepted extensions.
//   * SetAcceptedExtensionsStores     — extension_count() tracks the stored list.
//   * SimulateDropConsumeRoundTrip    — simulate_drop + consume_dropped_path happy path.
//   * ConsumeOnEmptyReturnsFalse      — consume_dropped_path returns false when nothing
//                                        is pending; out_path is not modified.
//   * FilterRejectsUnknownExtension   — simulate_drop with a filtered extension
//                                        list rejects non-matching paths.
//   * FilterAcceptsMatchingExtension  — matching extension passes the filter.
//   * EmptyFilterAcceptsAll           — cleared filter accepts any extension.
//   * DrawDefaultDoesNotCrash         — draw() with default state emits background.
//   * DrawZeroBoundsDoesNotCrash      — draw() on a zero-size rect returns early.
// =============================================================================
#include <cd/editor/panel_asset_drop_target/AssetDropTarget.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace adt = cd::editor::panel::asset_drop_target;

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, DefaultCtorIsEmpty)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, DefaultCtorIsEmpty)
{
    const adt::AssetDropTarget target;

    EXPECT_EQ(target.extension_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, SetAcceptedExtensionsStores)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, SetAcceptedExtensionsStores)
{
    adt::AssetDropTarget target;

    const std::vector<std::string> exts { ".gltf", ".png", ".wav" };
    target.set_accepted_extensions(exts);

    EXPECT_EQ(target.extension_count(), static_cast<std::size_t>(3U));

    // Replacing with a smaller list updates the count.
    const std::vector<std::string> two { ".obj", ".jpg" };
    target.set_accepted_extensions(two);
    EXPECT_EQ(target.extension_count(), static_cast<std::size_t>(2U));

    // Clearing with empty span.
    target.set_accepted_extensions({});
    EXPECT_EQ(target.extension_count(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, SimulateDropConsumeRoundTrip)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, SimulateDropConsumeRoundTrip)
{
    adt::AssetDropTarget target;

    // With no filter, any path is accepted.
    target.simulate_drop("/assets/hero.gltf");

    std::string out;
    const bool consumed = target.consume_dropped_path(out);

    EXPECT_TRUE(consumed);
    EXPECT_EQ(out, "/assets/hero.gltf");

    // After consuming, the buffer is empty.
    std::string out2 = "unchanged";
    EXPECT_FALSE(target.consume_dropped_path(out2));
    EXPECT_EQ(out2, "unchanged");
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, ConsumeOnEmptyReturnsFalse)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, ConsumeOnEmptyReturnsFalse)
{
    adt::AssetDropTarget target;

    std::string out = "sentinel";
    EXPECT_FALSE(target.consume_dropped_path(out));
    // out_path must not be modified when no drop is pending.
    EXPECT_EQ(out, "sentinel");
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, FilterRejectsUnknownExtension)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, FilterRejectsUnknownExtension)
{
    adt::AssetDropTarget target;

    const std::vector<std::string> exts { ".gltf", ".png" };
    target.set_accepted_extensions(exts);

    // .mp3 is not in the accepted list.
    target.simulate_drop("/audio/track.mp3");

    std::string out;
    EXPECT_FALSE(target.consume_dropped_path(out));
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, FilterAcceptsMatchingExtension)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, FilterAcceptsMatchingExtension)
{
    adt::AssetDropTarget target;

    const std::vector<std::string> exts { ".gltf", ".png" };
    target.set_accepted_extensions(exts);

    // .png is in the accepted list.
    target.simulate_drop("/textures/albedo.png");

    std::string out;
    EXPECT_TRUE(target.consume_dropped_path(out));
    EXPECT_EQ(out, "/textures/albedo.png");
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, EmptyFilterAcceptsAll)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, EmptyFilterAcceptsAll)
{
    adt::AssetDropTarget target;

    // Set a filter, then clear it.
    const std::vector<std::string> exts { ".gltf" };
    target.set_accepted_extensions(exts);
    target.set_accepted_extensions({});

    // Now anything should pass.
    target.simulate_drop("/anything.xyz");

    std::string out;
    EXPECT_TRUE(target.consume_dropped_path(out));
    EXPECT_EQ(out, "/anything.xyz");
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, DrawDefaultDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, DrawDefaultDoesNotCrash)
{
    adt::AssetDropTarget          target;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 300.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    target.draw(batcher, theme, bounds);

    // Background quad must have been emitted (at least one command).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(AssetDropTarget, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(AssetDropTarget, DrawZeroBoundsDoesNotCrash)
{
    adt::AssetDropTarget          target;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    target.draw(batcher, theme, zero);

    // bounds.is_valid() == false → early exit after the background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
