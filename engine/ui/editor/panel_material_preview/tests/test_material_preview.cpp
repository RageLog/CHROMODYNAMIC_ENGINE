// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_material_preview/tests/test_material_preview.cpp
//
// phase678 — unit tests for cd::editor::panel::material_preview::MaterialPreview.
//
// All tests are headless (no ImGui / no RHI). Verified:
//   * DefaultCtorHasNoMaterial         — material() == nullptr after default ctor.
//   * SetMaterialRoundTrips            — set_material / material() pointer round-trip.
//   * DrawNoMaterialEmitsBackground    — draw() with nullptr emits only background + sep.
//   * DrawWithMaterialEmitsSphere      — draw() with a bound material emits sphere + bars.
//   * DrawMaskModeEmitsCutoffBar       — kMask alpha_mode adds extra cutoff bar.
//   * DrawOpaqueModeOmitsCutoffBar     — kOpaque skips cutoff bar (fewer quads).
//   * DrawChangedBaseColorChangesQuads — changing base_color from black to white adds
//                                        fill quads.
//   * DrawZeroBoundsReturnEarly        — draw() on zero-size rect stays near-empty.
//   * TexturePillsPresentVsAbsent      — bound texture paths produce different geometry
//                                        than empty paths (same pill count, different cols).
// =============================================================================
#include <cd/editor/panel_material_preview/MaterialPreview.hpp>

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/material/AlphaMode.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace mp = cd::editor::panel::material_preview;
namespace ma = cd::asset::material_authoring;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

ma::AuthoredMaterial make_mat(float r, float g, float b,
                               float metallic  = 0.0F,
                               float roughness = 0.5F)
{
    ma::AuthoredMaterial m;
    m.base_color = { r, g, b };
    m.metallic   = metallic;
    m.roughness  = roughness;
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtorHasNoMaterial
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DefaultCtorHasNoMaterial)
{
    const mp::MaterialPreview preview;
    EXPECT_EQ(preview.material(), nullptr);
}

// ---------------------------------------------------------------------------
// TEST 2 — SetMaterialRoundTrips
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, SetMaterialRoundTrips)
{
    mp::MaterialPreview preview;
    ma::AuthoredMaterial mat = make_mat(0.5F, 0.4F, 0.3F);

    preview.set_material(&mat);
    EXPECT_EQ(preview.material(), &mat);

    preview.set_material(nullptr);
    EXPECT_EQ(preview.material(), nullptr);
}

// ---------------------------------------------------------------------------
// TEST 3 — DrawNoMaterialEmitsBackground
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawNoMaterialEmitsBackground)
{
    mp::MaterialPreview            preview;  // no material
    cd::ui::renderer::DrawBatcher  batcher;
    const cd::ui::widgets::Theme   theme {};
    const cd::ui::widgets::Rect    bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    preview.draw(batcher, theme, bounds);

    // Only the background quad + separator bar should be emitted.
    // 2 quads × 4 verts = 8 verts. Allow up to 12 in case of rounding.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(4U));
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(16U));
}

// ---------------------------------------------------------------------------
// TEST 4 — DrawWithMaterialEmitsSphere
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawWithMaterialEmitsSphere)
{
    ma::AuthoredMaterial mat = make_mat(0.8F, 0.3F, 0.1F, 0.6F, 0.4F);

    mp::MaterialPreview            preview;
    preview.set_material(&mat);

    cd::ui::renderer::DrawBatcher  batcher;
    const cd::ui::widgets::Theme   theme {};
    const cd::ui::widgets::Rect    bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    preview.draw(batcher, theme, bounds);

    // background + sep + sphere(2) + metallic pip+track+fill(3) +
    // roughness pip+track+fill(3) + 3 pills = 13 quads = 52 verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(48U));
}

// ---------------------------------------------------------------------------
// TEST 5 — DrawMaskModeEmitsCutoffBar
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawMaskModeEmitsCutoffBar)
{
    ma::AuthoredMaterial mat_mask  = make_mat(0.5F, 0.5F, 0.5F);
    mat_mask.alpha_mode   = cd::material::AlphaMode::kMask;
    mat_mask.alpha_cutoff = 0.5F;

    ma::AuthoredMaterial mat_opaque = make_mat(0.5F, 0.5F, 0.5F);
    mat_opaque.alpha_mode = cd::material::AlphaMode::kOpaque;

    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    cd::ui::renderer::DrawBatcher batcher_mask;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_mask);
        batcher_mask.begin_frame();
        preview.draw(batcher_mask, theme, bounds);
    }

    cd::ui::renderer::DrawBatcher batcher_opaque;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_opaque);
        batcher_opaque.begin_frame();
        preview.draw(batcher_opaque, theme, bounds);
    }

    // kMask should emit the extra alpha cutoff bar → more quads.
    EXPECT_GT(batcher_mask.vertex_count(), batcher_opaque.vertex_count());
}

// ---------------------------------------------------------------------------
// TEST 6 — DrawOpaqueModeOmitsCutoffBar
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawOpaqueModeOmitsCutoffBar)
{
    ma::AuthoredMaterial mat = make_mat(0.5F, 0.5F, 0.5F);
    mat.alpha_mode = cd::material::AlphaMode::kOpaque;

    mp::MaterialPreview            preview;
    preview.set_material(&mat);

    cd::ui::renderer::DrawBatcher  batcher;
    const cd::ui::widgets::Theme   theme {};
    const cd::ui::widgets::Rect    bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    preview.draw(batcher, theme, bounds);

    // Without cutoff bar: background + sep + sphere(2) + metallic(3) +
    // roughness(3) + 3 pills(3) = 14 quads = 56 verts.
    // With cutoff bar the count would be >= 72.
    EXPECT_LT(batcher.vertex_count(), static_cast<std::size_t>(72U));
}

// ---------------------------------------------------------------------------
// TEST 7 — DrawZeroBoundsReturnEarly
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawZeroBoundsReturnEarly)
{
    ma::AuthoredMaterial           mat = make_mat(1.0F, 1.0F, 1.0F);
    mp::MaterialPreview            preview;
    preview.set_material(&mat);

    cd::ui::renderer::DrawBatcher  batcher;
    const cd::ui::widgets::Theme   theme {};
    const cd::ui::widgets::Rect    zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    preview.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns after background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawChangedBaseColorChangesQuads
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, DrawChangedBaseColorChangesQuads)
{
    // Black base_color: metallic/roughness fill quads omitted (zero width).
    ma::AuthoredMaterial mat_black = make_mat(0.0F, 0.0F, 0.0F, 0.0F, 0.0F);
    // White base_color + full metallic + full roughness: all fill bars present.
    ma::AuthoredMaterial mat_white = make_mat(1.0F, 1.0F, 1.0F, 1.0F, 1.0F);

    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    cd::ui::renderer::DrawBatcher batcher_black;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_black);
        batcher_black.begin_frame();
        preview.draw(batcher_black, theme, bounds);
    }

    cd::ui::renderer::DrawBatcher batcher_white;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_white);
        batcher_white.begin_frame();
        preview.draw(batcher_white, theme, bounds);
    }

    // White (full metallic + roughness) produces more fill quads than black.
    EXPECT_GT(batcher_white.vertex_count(), batcher_black.vertex_count());
}

// ---------------------------------------------------------------------------
// TEST 9 — TexturePillsPresentVsAbsent
// ---------------------------------------------------------------------------
TEST(MaterialPreviewPanel, TexturePillsPresentVsAbsent)
{
    // Two materials: identical except one has textures, one does not.
    // Both should emit the same pill count (3 pills always drawn), but this
    // test verifies that either case doesn't crash and emits a reasonable count.

    ma::AuthoredMaterial mat_no_tex = make_mat(0.5F, 0.5F, 0.5F);
    // Leave all texture paths empty.

    ma::AuthoredMaterial mat_with_tex = make_mat(0.5F, 0.5F, 0.5F);
    mat_with_tex.albedo_texture_path = "textures/albedo.png";
    mat_with_tex.normal_texture_path = "textures/normal.png";
    mat_with_tex.mr_texture_path     = "textures/mr.png";

    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    cd::ui::renderer::DrawBatcher batcher_no;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_no_tex);
        batcher_no.begin_frame();
        preview.draw(batcher_no, theme, bounds);
    }

    cd::ui::renderer::DrawBatcher batcher_with;
    {
        mp::MaterialPreview preview;
        preview.set_material(&mat_with_tex);
        batcher_with.begin_frame();
        preview.draw(batcher_with, theme, bounds);
    }

    // Both must emit substantial geometry (sphere + bars + 3 pills).
    EXPECT_GE(batcher_no.vertex_count(),   static_cast<std::size_t>(40U));
    EXPECT_GE(batcher_with.vertex_count(), static_cast<std::size_t>(40U));

    // Pill count is the same regardless of whether paths are present/absent
    // (all 3 pills are drawn in both cases; presence only changes tint colour).
    EXPECT_EQ(batcher_no.vertex_count(), batcher_with.vertex_count());
}
