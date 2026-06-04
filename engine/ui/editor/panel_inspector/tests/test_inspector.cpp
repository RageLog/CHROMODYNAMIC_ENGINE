// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_inspector/tests/test_inspector.cpp
//
// phase543 — unit tests for cd::editor::panel::inspector::Inspector.
//
// All tests are headless (no ImGui / no RHI). The ImGui draw_imgui() path
// requires a live ImGui context; those tests are excluded here. We verify:
//   * Default construction produces a no-selection, null-world state.
//   * set_target / get_selected round-trips.
//   * set_world_ptr stores and clear(nullptr) clears.
//   * draw() runs without crashing when no selection is set.
//   * draw() runs without crashing when a valid entity + LocalTransform are set.
//   * Selection change (re-target to a different entity) is tracked correctly.
// =============================================================================
#include <cd/editor/panel_inspector/Inspector.hpp>

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/material/Material.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace insp = cd::editor::panel::inspector;

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, DefaultCtorHasNoSelection)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, DefaultCtorHasNoSelection)
{
    const insp::Inspector inspector;
    const cd::ecs::Entity sel = inspector.get_selected();
    EXPECT_EQ(sel.id,         0U);
    EXPECT_EQ(sel.generation, 0U);
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, SetTargetRoundTrips)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, SetTargetRoundTrips)
{
    insp::Inspector inspector;
    const cd::ecs::Entity e { 42U, 7U };
    inspector.set_target(e);

    const cd::ecs::Entity got = inspector.get_selected();
    EXPECT_EQ(got.id,         42U);
    EXPECT_EQ(got.generation, 7U);
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, SetWorldPtrStoresAndClear)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, SetWorldPtrStoresAndClear)
{
    insp::Inspector inspector;
    cd::ecs::World  world;

    inspector.set_world_ptr(&world);
    // No public getter for the pointer — just verify no crash and that
    // draw() with a valid entity picks it up (tested in DrawNoSelection).

    inspector.set_world_ptr(nullptr);
    // No crash on nullptr.
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, DrawNoSelectionDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, DrawNoSelectionDoesNotCrash)
{
    insp::Inspector       inspector;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    inspector.draw(batcher, theme, bounds);

    // Background quad should have been emitted (at least one command).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, DrawWithValidEntityEmitsQuads)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, DrawWithValidEntityEmitsQuads)
{
    cd::ecs::World  world;
    cd::scene::Scene scene { world };
    const cd::ecs::Entity e = scene.create_node();

    // Set a non-identity position so the fill strips have non-zero widths.
    auto* lt = scene.local(e);
    ASSERT_NE(lt, nullptr);
    lt->value.position = { 5.0F, -2.0F, 3.0F };
    lt->value.scale    = { 1.0F,  2.0F, 0.5F };

    insp::Inspector inspector;
    inspector.set_target(e);
    inspector.set_world_ptr(&world);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    inspector.draw(batcher, theme, bounds);

    // The batcher merges quads with the same state into a single draw command.
    // Expect at least: background (4) + separator (4) = 8 verts minimum.
    // With a valid entity + LocalTransform the field rows add more.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
    EXPECT_GE(batcher.vertex_count(),  static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, RetargetChangesSelection)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, RetargetChangesSelection)
{
    cd::ecs::World   world;
    cd::scene::Scene scene { world };

    const cd::ecs::Entity e1 = scene.create_node();
    const cd::ecs::Entity e2 = scene.create_node();

    insp::Inspector inspector;
    inspector.set_world_ptr(&world);

    inspector.set_target(e1);
    EXPECT_EQ(inspector.get_selected().id, e1.id);

    inspector.set_target(e2);
    EXPECT_EQ(inspector.get_selected().id, e2.id);

    // Clear selection.
    inspector.set_target({});
    EXPECT_EQ(inspector.get_selected().id, 0U);
}

// ---------------------------------------------------------------------------
// phase667 / M12 W2 — MaterialInstance round-trip
// ---------------------------------------------------------------------------

TEST(InspectorPanel, MaterialInstanceBindingDefaultsAreNullSafe)
{
    insp::Inspector inspector;

    // No material bound: getters return MaterialInstance defaults; setters
    // are no-ops (no crash).
    EXPECT_EQ(inspector.material_instance(), nullptr);
    EXPECT_FLOAT_EQ(inspector.metallic(),  0.0F);
    EXPECT_FLOAT_EQ(inspector.roughness(), 0.5F);
    EXPECT_EQ(inspector.alpha_mode(), cd::material::AlphaMode::kOpaque);
    EXPECT_FLOAT_EQ(inspector.alpha_cutoff(), 0.5F);

    inspector.set_metallic(0.9F);
    inspector.set_roughness(0.1F);
    inspector.set_alpha_mode(cd::material::AlphaMode::kBlend);
    inspector.set_alpha_cutoff(0.25F);

    // Still no instance bound → no observable side effect, defaults persist.
    EXPECT_EQ(inspector.material_instance(), nullptr);
    EXPECT_FLOAT_EQ(inspector.metallic(),  0.0F);
    EXPECT_FLOAT_EQ(inspector.roughness(), 0.5F);
    EXPECT_EQ(inspector.alpha_mode(), cd::material::AlphaMode::kOpaque);
}

TEST(InspectorPanel, MaterialInstancePbrRoundTrip)
{
    insp::Inspector                  inspector;
    cd::material::MaterialInstance   mi;  // inert default-constructed instance

    inspector.set_material_instance(&mi);
    EXPECT_EQ(inspector.material_instance(), &mi);

    // metallic / roughness round-trip via the Inspector accessors.
    inspector.set_metallic(0.75F);
    inspector.set_roughness(0.20F);
    EXPECT_FLOAT_EQ(inspector.metallic(),  0.75F);
    EXPECT_FLOAT_EQ(inspector.roughness(), 0.20F);
    // Underlying MaterialInstance should see the same state.
    EXPECT_FLOAT_EQ(mi.metallic(),  0.75F);
    EXPECT_FLOAT_EQ(mi.roughness(), 0.20F);

    // Out-of-range input is clamped by MaterialInstance internally.
    inspector.set_metallic(2.5F);
    inspector.set_roughness(-1.0F);
    EXPECT_FLOAT_EQ(inspector.metallic(),  1.0F);
    EXPECT_FLOAT_EQ(inspector.roughness(), 0.0F);
}

TEST(InspectorPanel, MaterialInstanceAlphaRoundTrip)
{
    insp::Inspector                  inspector;
    cd::material::MaterialInstance   mi;

    inspector.set_material_instance(&mi);

    inspector.set_alpha_mode(cd::material::AlphaMode::kMask);
    inspector.set_alpha_cutoff(0.35F);
    EXPECT_EQ(inspector.alpha_mode(), cd::material::AlphaMode::kMask);
    EXPECT_FLOAT_EQ(inspector.alpha_cutoff(), 0.35F);
    EXPECT_EQ(mi.alpha_mode(),         cd::material::AlphaMode::kMask);
    EXPECT_FLOAT_EQ(mi.alpha_cutoff(), 0.35F);

    inspector.set_alpha_mode(cd::material::AlphaMode::kBlend);
    EXPECT_EQ(inspector.alpha_mode(), cd::material::AlphaMode::kBlend);

    inspector.set_alpha_mode(cd::material::AlphaMode::kOpaque);
    EXPECT_EQ(inspector.alpha_mode(), cd::material::AlphaMode::kOpaque);

    // Detach.
    inspector.set_material_instance(nullptr);
    EXPECT_EQ(inspector.material_instance(), nullptr);
}

TEST(InspectorPanel, DrawWithMaterialInstanceEmitsExtraQuads)
{
    cd::ecs::World   world;
    cd::scene::Scene scene { world };
    const cd::ecs::Entity e = scene.create_node();

    insp::Inspector                  inspector;
    cd::material::MaterialInstance   mi;
    mi.set_metallic(0.7F);
    mi.set_roughness(0.4F);
    mi.set_alpha_mode(cd::material::AlphaMode::kMask);
    mi.set_alpha_cutoff(0.6F);

    inspector.set_target(e);
    inspector.set_world_ptr(&world);
    inspector.set_material_instance(&mi);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme  {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 800.0F };

    batcher.begin_frame();
    inspector.draw(batcher, theme, bounds);

    // With both target + material bound the PBR section adds:
    //   * 1 section separator quad
    //   * 2 (background + fill) per metallic + roughness slider  = 4
    //   * 3 alpha mode cells                                     = 3
    //   * 2 (background + fill) for the alpha_cutoff slider      = 2
    // Plus the LocalTransform 3 rows. Combined vertex count is well above
    // the 8-vert minimum the no-PBR variant produces.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(32U));
}

// ---------------------------------------------------------------------------
// TEST(InspectorPanel, DrawZeroBoundsSkipsRows)
// ---------------------------------------------------------------------------
TEST(InspectorPanel, DrawZeroBoundsSkipsRows)
{
    cd::ecs::World   world;
    cd::scene::Scene scene { world };
    const cd::ecs::Entity e = scene.create_node();

    insp::Inspector inspector;
    inspector.set_target(e);
    inspector.set_world_ptr(&world);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero_bounds { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not crash — that's the sole contract for a zero-size bounds.
    inspector.draw(batcher, theme, zero_bounds);

    // bounds.is_valid() == false → draw() returns early after the background
    // quad (which the batcher may or may not emit for a zero-size rect).
    // Row quads must NOT be emitted, so vertex count stays very low.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
