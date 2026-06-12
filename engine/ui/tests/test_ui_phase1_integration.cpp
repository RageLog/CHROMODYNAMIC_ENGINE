// =============================================================================
// CHROMODYNAMIC — cd::ui Phase 1 integration tests
//
// End-to-end smoke for the layout + font + renderer pipeline added in
// phases 442-444. Proves the three libraries compose without an RHI
// or a real platform window: layout produces world rects, font produces
// glyph atlas UVs, the batcher consumes both to emit one frame of UI.
//
// This is the same shape the future Phase 1.5 `hello_ui` sample will
// use; here we exercise it as a CPU-only unit test so the integration
// is locked in before the RHI side (Phase 1.2b) lands.
// =============================================================================
#include <cd/ui/font/Font.hpp>
#include <cd/ui/layout/Flex.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ll = cd::ui::layout;
namespace uf = cd::ui::font;
namespace ur = cd::ui::renderer;

namespace
{

[[nodiscard]] std::vector<std::uint8_t> find_system_font()
{
    static const std::array<const char*, 4> k_candidates {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : k_candidates)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) continue;
        const auto sz = static_cast<std::size_t>(f.tellg());
        f.seekg(0);
        std::vector<std::uint8_t> out(sz);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
        if (!out.empty()) return out;
    }
    return {};
}

}  // namespace

// ---- Layout → batcher (no font needed): emit one rect per leaf node ------

TEST(UiPhase1Integration, LayoutFeedsBatcherProducesQuadPerLeaf)
{
    // Build a Flex tree: row with three equal-grow children.
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle child_s;
    child_s.flex_grow = 1.0F;
    auto a = t.create_node(child_s);
    auto b = t.create_node(child_s);
    auto c = t.create_node(child_s);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, c);

    t.solve(root, 600.0F, 100.0F);

    // Batcher consumes the computed rects.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (auto id : { a, b, c })
    {
        const auto r = t.layout(id);
        batcher.quad(r.x, r.y, r.width, r.height,
                     ur::Color { 80U, 100U, 140U, 255U });
    }

    // 3 quads × 4 verts = 12; × 6 idx = 18; all merged into one command
    // because state (variant + texture_slot + scissor) is identical.
    EXPECT_EQ(batcher.vertex_count(), 12U);
    EXPECT_EQ(batcher.index_count(),  18U);
    ASSERT_EQ(batcher.command_count(), 1U);
    EXPECT_EQ(batcher.commands()[0].variant, ur::material::kSolid);
}

// ---- Layout + font + batcher: render one label of glyphs into a panel ----

TEST(UiPhase1Integration, LayoutAndFontFeedTextIntoBatcher)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF available -- skipping text-render integration test";
    }

    // 1) Layout: column root with one inner panel (40 px tall) for the label.
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.direction = ll::FlexDirection::kColumn;
    root_s.padding = { 8.0F, 8.0F, 8.0F, 8.0F };
    auto root = t.create_node(root_s);

    ll::FlexStyle panel_s;
    panel_s.height = 40.0F;
    auto panel = t.create_node(panel_s);
    t.add_child(root, panel);

    t.solve(root, 400.0F, 200.0F);
    const auto panel_rect = t.layout(panel);
    EXPECT_GT(panel_rect.width, 0.0F);
    EXPECT_GT(panel_rect.height, 0.0F);

    // 2) Font: rasterize ASCII for the label.
    uf::Font font;
    ASSERT_TRUE(font.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(font.rasterize_range(0x0020U, 0x007EU, 18.0F, 1024U));

    // 3) Batcher: draw panel background, then "Hi" glyphs inside.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(panel_rect.x, panel_rect.y, panel_rect.width, panel_rect.height,
                 ur::Color { 40U, 44U, 56U, 255U });

    const char* text = "Hi";
    float pen_x = panel_rect.x + 8.0F;
    const float baseline_y = panel_rect.y + panel_rect.height * 0.5F;
    for (const char* p = text; *p != '\0'; ++p)
    {
        auto g = font.glyph_uv(static_cast<std::uint32_t>(*p));
        if (!g.has_value()) continue;
        if (g->width > 0.0F && g->height > 0.0F)
        {
            batcher.glyph(
                pen_x + g->bearing_x,
                baseline_y - g->bearing_y,
                g->width, g->height,
                /* atlas slot */ 0U,
                { g->u0, g->v0, g->u1, g->v1 },
                ur::Color::white());
        }
        pen_x += g->advance;
    }

    // Expect 1 solid quad command (background) + 1 glyph command (merged
    // 'H' + 'i' since same atlas slot + variant).
    ASSERT_GE(batcher.command_count(), 2U);
    EXPECT_EQ(batcher.commands()[0].variant, ur::material::kSolid);
    EXPECT_EQ(batcher.commands()[1].variant, ur::material::kGlyph);
    EXPECT_EQ(batcher.commands()[1].texture_slot, 0U);

    // 'H' + 'i' both have visible glyphs at 18 px (no whitespace).
    EXPECT_EQ(batcher.commands()[1].index_count, 12U);  // 2 glyphs × 6 idx
}
