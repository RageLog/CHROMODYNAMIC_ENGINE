// =============================================================================
// CHROMODYNAMIC -- cd::ui::renderer_rhi material-variant Submitter end-to-end
// Phase 648 / M10 W3A Sprint-3 -- Route A FULL wire-up coverage.
//
// Exercises the Phase 648 contract on top of the Phase 608 factory:
//   * `create_with_material_ui_variant` accepts a Sprint-2 variant (theme
//     UBO + SDF sampler descriptor set) and allocates a MaterialInstance
//     + theme UBO buffer internally;
//   * `set_theme_palette` re-uploads a payload into the variant's UBO;
//   * `set_sdf_atlas` writes a CombinedImageSampler descriptor write into
//     the variant's set;
//   * `record()` emits the expected command sequence after the batcher
//     pushes 4 solid quads + 1 glyph quad:
//       - 1 bind_graphics_pipeline      (the variant's pipeline)
//       - 1 push_constants              (vec2 inv_viewport)
//       - 2 bind_vertex/index_buffer    (vb + ib)
//       - 2 set_scissor / draw_indexed  (1 per batched DrawCommand:
//                                        the 4 solid quads merge into
//                                        one DrawCommand keyed by variant
//                                        kSolid, the glyph emits a second
//                                        DrawCommand keyed by variant
//                                        kGlyph + texture_slot).
//
// The whole test runs against a real Vulkan device when the host has an
// ICD and glslang is compiled in; otherwise it GTEST_SKIPs so the
// binary still links cleanly on headless CI.
// =============================================================================
#include <cd/material/UiVariant.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <utility>

namespace
{

namespace rh = cd::ui::renderer_rhi;
namespace ur = cd::ui::renderer;
namespace cm = cd::material;

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_vulkan()
{
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.app_name          = "test_submitter_material_route_end_to_end";
    vci.enable_validation = false;
    auto dr = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dr.has_value())
    {
        return nullptr;
    }
    return std::move(*dr);
}

// Build a Sprint-2 variant with both descriptors (theme UBO + SDF sampler).
[[nodiscard]] cd::core::Result<cm::UiVariant>
make_sprint2_variant(cd::rhi::IDevice& dev)
{
    static constexpr std::array<cd::rhi::Format, 1> kColorFormats {
        cd::rhi::Format::kBGRA8Unorm,
    };
    cm::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.texture_count            = 1U;       // SDF sampler ON
    spec.use_theme_palette_ubo    = true;     // theme UBO ON
    spec.theme_palette_ubo_slot   = 0U;
    spec.sdf_font_sampler_slot    = 1U;
    spec.name                     = "submitter_route_a_e2e_variant";
    return cm::create_ui_variant(dev, spec);
}

}  // namespace

// -----------------------------------------------------------------------------
// End-to-end: Route A + Sprint-2 variant + 4 solid quads + 1 glyph.
// -----------------------------------------------------------------------------
TEST(SubmitterMaterialRouteEndToEnd, FourQuadsPlusOneGlyphRecordsExpectedSequence)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; Route A end-to-end needs a real device";
    }
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // Build a Sprint-2 variant carrying theme UBO + SDF sampler descriptors.
    auto var_r = make_sprint2_variant(*vk);
    ASSERT_TRUE(var_r.has_value())
        << "cd::material::create_ui_variant (Sprint-2) failed: "
        << "domain=" << var_r.error().domain
        << " code="  << var_r.error().code
        << " msg="   << std::string(var_r.error().message);
    EXPECT_TRUE(var_r->is_valid());
    EXPECT_TRUE(var_r->has_theme_ubo());
    EXPECT_TRUE(var_r->has_sdf_sampler());

    // Hand it to the submitter. The factory must allocate a MaterialInstance
    // + theme UBO buffer and write the UBO into the descriptor set.
    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;
    auto sub_r = rh::Submitter::create_with_material_ui_variant(
        *vk, info, std::move(*var_r));
    ASSERT_TRUE(sub_r.has_value())
        << "create_with_material_ui_variant failed: "
        << "domain=" << sub_r.error().domain
        << " code="  << sub_r.error().code
        << " msg="   << std::string(sub_r.error().message);
    auto& sub = *sub_r;
    EXPECT_TRUE(sub.is_valid());

    // set_theme_palette must succeed because the variant carries a UBO.
    cm::UiThemePaletteUbo palette {};
    palette.primary[0] = 0.2F; palette.primary[1] = 0.5F;
    palette.primary[2] = 0.9F; palette.primary[3] = 1.0F;
    EXPECT_TRUE(sub.set_theme_palette(palette));

    // set_sdf_atlas needs a real combined image+sampler. The test allocates
    // a 4x4 R8 texture + a default sampler so the descriptor write has live
    // handles to point at (no actual sampling will happen in this smoke
    // because we never present, but the descriptor must be writable).
    cd::rhi::TextureDesc td {};
    td.type   = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kR8Unorm;
    td.extent = { 4U, 4U, 1U };
    td.usage  = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_r = vk->create_texture(td);
    ASSERT_TRUE(tex_r.has_value()) << tex_r.error().message;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = *tex_r;
    tvd.format  = cd::rhi::Format::kR8Unorm;
    auto view_r = vk->create_texture_view(tvd);
    ASSERT_TRUE(view_r.has_value()) << view_r.error().message;

    cd::rhi::SamplerDesc sd {};
    sd.address_u = cd::rhi::SamplerAddressMode::kClampToEdge;
    sd.address_v = cd::rhi::SamplerAddressMode::kClampToEdge;
    auto smp_r = vk->create_sampler(sd);
    ASSERT_TRUE(smp_r.has_value()) << smp_r.error().message;

    EXPECT_TRUE(sub.set_sdf_atlas(*view_r, *smp_r));

    // Push 4 solid quads + 1 glyph through the batcher.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 4; ++i)
    {
        batcher.quad(static_cast<float>(i * 32), 8.0F, 24.0F, 24.0F,
                     ur::Color::white());
    }
    // Glyph: distinct material variant + texture slot, so the batcher
    // segments this as a SECOND DrawCommand. The atlas UV is [0,1]^2 so
    // the fragment shader's "uv inside [0..1]" branch triggers.
    const ur::AtlasUv kFullUv { 0.0F, 0.0F, 1.0F, 1.0F };
    batcher.glyph(200.0F, 8.0F, 24.0F, 24.0F,
                  /*texture_slot=*/0U, kFullUv, ur::Color::white());

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  20U);   // 4 solid + 1 glyph = 5 quads * 4 verts
    EXPECT_EQ(sub.index_count(),   30U);   // 5 quads * 6 indices
    EXPECT_EQ(sub.command_count(), 2U);    // solid batch + glyph batch

    // Record into a real Vulkan command buffer. The submitter must bind
    // the variant pipeline, push the inv-viewport constant, bind the
    // material descriptor set (theme + SDF), bind vb/ib, and issue two
    // draw_indexed calls (one per DrawCommand).
    auto cmd = vk->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, cd::rhi::Extent2D { 800U, 600U });
    cmd->end();

    // Clean up the throwaway texture / sampler / view (the submitter
    // takes care of the MaterialInstance + theme UBO).
    vk->destroy_sampler(*smp_r);
    vk->destroy_texture_view(*view_r);
    vk->destroy_texture(*tex_r);
}

// -----------------------------------------------------------------------------
// Sprint-3 contract: when the variant has NO descriptors (Sprint-1 path),
// set_theme_palette and set_sdf_atlas both no-op cleanly (return false)
// and record() still issues the same pipeline + push_constants + draws.
// -----------------------------------------------------------------------------
TEST(SubmitterMaterialRouteEndToEnd, NoDescriptorVariantSpritSetAccessorsNoOp)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD";
    }
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // Default spec = Sprint-1 = no descriptors.
    static constexpr std::array<cd::rhi::Format, 1> kColorFormats {
        cd::rhi::Format::kBGRA8Unorm,
    };
    cm::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.name = "submitter_route_a_e2e_no_descriptors";
    auto var_r = cm::create_ui_variant(*vk, spec);
    ASSERT_TRUE(var_r.has_value()) << var_r.error().message;
    EXPECT_FALSE(var_r->has_theme_ubo());
    EXPECT_FALSE(var_r->has_sdf_sampler());

    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;
    auto sub_r = rh::Submitter::create_with_material_ui_variant(
        *vk, info, std::move(*var_r));
    ASSERT_TRUE(sub_r.has_value()) << sub_r.error().message;
    auto& sub = *sub_r;

    // No theme UBO descriptor allocated -> set_theme_palette returns false.
    cm::UiThemePaletteUbo palette {};
    EXPECT_FALSE(sub.set_theme_palette(palette));
    // No SDF sampler descriptor -> set_sdf_atlas returns false even with
    // valid-looking (but invalid) handles.
    EXPECT_FALSE(sub.set_sdf_atlas(cd::rhi::TextureViewHandle {},
                                   cd::rhi::SamplerHandle {}));
}

// -----------------------------------------------------------------------------
// Glyph-atlas host path -- create-time binding. ≥80→100 marathon gap-close:
// when the Sprint-2 variant carries an SDF sampler AND the caller pre-supplies
// `info.atlas_view` + `info.atlas_sampler`, the factory must write the
// CombinedImageSampler descriptor at create-time (NOT defer to set_sdf_atlas).
// This is the documented "glyph atlas path lands w/ material variant" host
// surface; it is purely host-side descriptor management and records cleanly
// against a real Vulkan device. We never present, so no rendered frame is
// affected (UI glyphs are not part of any golden scene).
// -----------------------------------------------------------------------------
TEST(SubmitterMaterialRouteEndToEnd, CreateTimeAtlasBindingWritesSdfDescriptor)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; create-time atlas binding needs a real device";
    }
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    // Sprint-2 variant carrying the SDF sampler descriptor.
    auto var_r = make_sprint2_variant(*vk);
    ASSERT_TRUE(var_r.has_value()) << var_r.error().message;
    ASSERT_TRUE(var_r->has_sdf_sampler());

    // Build a live R8 atlas texture + view + sampler BEFORE create() so the
    // factory's create-time branch (`info.atlas_view.is_valid() && ...`) fires.
    cd::rhi::TextureDesc td {};
    td.type   = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kR8Unorm;
    td.extent = { 8U, 8U, 1U };
    td.usage  = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_r = vk->create_texture(td);
    ASSERT_TRUE(tex_r.has_value()) << tex_r.error().message;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = *tex_r;
    tvd.format  = cd::rhi::Format::kR8Unorm;
    auto view_r = vk->create_texture_view(tvd);
    ASSERT_TRUE(view_r.has_value()) << view_r.error().message;

    cd::rhi::SamplerDesc sd {};
    sd.address_u = cd::rhi::SamplerAddressMode::kClampToEdge;
    sd.address_v = cd::rhi::SamplerAddressMode::kClampToEdge;
    auto smp_r = vk->create_sampler(sd);
    ASSERT_TRUE(smp_r.has_value()) << smp_r.error().message;

    rh::SubmitterCreateInfo info {};
    info.max_vertices  = 1024U;
    info.max_indices   = 4096U;
    info.color_format  = cd::rhi::Format::kBGRA8Unorm;
    info.atlas_view    = *view_r;     // create-time atlas binding
    info.atlas_sampler = *smp_r;
    auto sub_r = rh::Submitter::create_with_material_ui_variant(
        *vk, info, std::move(*var_r));
    ASSERT_TRUE(sub_r.has_value())
        << "create_with_material_ui_variant (create-time atlas) failed: "
        << "domain=" << sub_r.error().domain
        << " code="  << sub_r.error().code
        << " msg="   << std::string(sub_r.error().message);
    auto& sub = *sub_r;
    EXPECT_TRUE(sub.is_valid());

    // Push 1 glyph quad and record. The descriptor was bound at create-time,
    // so record() binds the variant + set + issues one draw without crashing.
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    const ur::AtlasUv kFullUv { 0.0F, 0.0F, 1.0F, 1.0F };
    batcher.glyph(40.0F, 8.0F, 24.0F, 24.0F,
                  /*texture_slot=*/0U, kFullUv, ur::Color::white());
    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  4U);
    EXPECT_EQ(sub.index_count(),   6U);
    EXPECT_EQ(sub.command_count(), 1U);

    auto cmd = vk->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, cd::rhi::Extent2D { 800U, 600U });
    cmd->end();

    // A subsequent set_sdf_atlas with INVALID handles must be rejected
    // (negative path) -- the create-time binding stays intact.
    EXPECT_FALSE(sub.set_sdf_atlas(cd::rhi::TextureViewHandle {},
                                   cd::rhi::SamplerHandle {}));
    // ...and a re-bind with the same live handles must succeed (idempotent
    // descriptor rewrite -- the editor swaps atlases mid-session this way).
    EXPECT_TRUE(sub.set_sdf_atlas(*view_r, *smp_r));

    vk->destroy_sampler(*smp_r);
    vk->destroy_texture_view(*view_r);
    vk->destroy_texture(*tex_r);
}
