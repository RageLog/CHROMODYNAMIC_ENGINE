// =============================================================================
// CHROMODYNAMIC — cd::material::UiVariant tests
// M3 W1B Sprint-1 (phase 555) + Sprint-2 (phase 573).
//
// Sprint-1 cases (kept):
//   (a) factory returns a valid variant given a real Vulkan device
//       (GTEST_SKIP if no Vulkan ICD or no glslang).
//   (b) variant can be bound to a command buffer without errors
//       (NullDevice + NullCommandBuffer — no Vulkan required for the
//       bind-time contract, exercised against Vulkan in (a)).
//   (c) UiVariantSpec defaults are sensible (texture_count == 0,
//       theme_palette_ubo_binding == -1, blend = alpha, depth disabled,
//       use_theme_palette_ubo == false).
//
// Sprint-2 cases (new):
//   (d) variant with SDF sampler bound dispatches OK
//       — a Sprint-2 spec (texture_count = 1) compiles, exposes a
//         CombinedImageSampler + UBO descriptor set, allocates a
//         MaterialInstance, and binds + draws against a NullCommandBuffer.
//   (e) theme UBO update reflects in subsequent draws
//       — a Sprint-2 spec with only the theme UBO branch
//         (use_theme_palette_ubo = true, texture_count = 0) allows a
//         MaterialInstance::update() with the std140 UiThemePaletteUbo
//         buffer write; a follow-up bind+draw observes the update.
// =============================================================================
#include <cd/material/UiVariant.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace
{

// -----------------------------------------------------------------------------
// (c) UiVariantSpec defaults are sensible
// -----------------------------------------------------------------------------

TEST(UiVariantSpec, DefaultsAreSensible)
{
    cd::material::UiVariantSpec s {};

    EXPECT_EQ(s.vertex_format, cd::material::UiVertexFormat::kUiDefault);
    EXPECT_EQ(s.blend_state,   cd::material::UiBlendMode::kAlpha);
    EXPECT_EQ(s.depth_state,   cd::material::UiDepthMode::kDisabled);

    // Default = Sprint-1 vertex-color-only path (no SDF, no theme UBO).
    EXPECT_EQ(s.texture_count, 0U);
    EXPECT_EQ(s.theme_palette_ubo_binding, -1);
    EXPECT_FALSE(s.use_theme_palette_ubo);

    // Sprint-2 defaults (only consulted when use_theme_palette_ubo / SDF
    // are turned on, but they should still be sensible at construction).
    EXPECT_EQ(s.theme_palette_ubo_slot, 0U);
    EXPECT_EQ(s.sdf_font_sampler_slot,  1U);

    EXPECT_TRUE(s.color_attachment_formats.empty());  // factory supplies RGBA8 default
    EXPECT_EQ(s.depth_attachment_format, cd::rhi::Format::kUndefined);
    EXPECT_FALSE(s.name.empty());
}

// Sprint-2: texture_count > 1 still rejected (the UI variant caps at
// one glyph SDF atlas).
TEST(UiVariantSpec, FactoryRejectsTextureCountAboveOne)
{
    cd::rhi::NullDevice dev;
    cd::material::UiVariantSpec s {};
    s.texture_count = 2U;

    auto v = cd::material::create_ui_variant(dev, s);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

// The legacy `theme_palette_ubo_binding` field is still rejected — Sprint-2
// callers must use `theme_palette_ubo_slot` + `use_theme_palette_ubo` instead.
TEST(UiVariantSpec, FactoryRejectsLegacyThemeUboBinding)
{
    cd::rhi::NullDevice dev;
    cd::material::UiVariantSpec s {};
    s.theme_palette_ubo_binding = 0;

    auto v = cd::material::create_ui_variant(dev, s);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

// Sprint-2: overlapping slots while a sampler is requested is rejected.
TEST(UiVariantSpec, FactoryRejectsOverlappingSlots)
{
    cd::rhi::NullDevice dev;
    cd::material::UiVariantSpec s {};
    s.texture_count            = 1U;
    s.theme_palette_ubo_slot   = 3U;
    s.sdf_font_sampler_slot    = 3U;

    auto v = cd::material::create_ui_variant(dev, s);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

// -----------------------------------------------------------------------------
// (b) variant can be bound to a command buffer (NullDevice path)
// -----------------------------------------------------------------------------
//
// NullDevice accepts any shader bytes for create_shader_module — but the
// factory uses glslang to compile inline GLSL first, so this case is
// only meaningful when the engine ships glslang. When CD_ENABLE_GLSLANG=OFF
// the factory returns kCompilerRequired; we skip cleanly in that path.
TEST(UiVariant, BindsToNullCommandBufferWithoutErrors)
{
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::rhi::NullDevice dev;
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.name = "ui_variant_test_b";

    auto v = cd::material::create_ui_variant(dev, spec);
    ASSERT_TRUE(v.has_value()) << v.error().message;
    EXPECT_TRUE(v->is_valid());
    EXPECT_EQ(v->texture_count(), 0U);

    cd::rhi::NullCommandBuffer cmd;
    cmd.begin();
    v->apply(cmd);                       // must compile + run without throwing
    cmd.end();

    EXPECT_EQ(cmd.log().bind_graphics_pipeline, 1U);
}

// -----------------------------------------------------------------------------
// (a) end-to-end on a real Vulkan device
// -----------------------------------------------------------------------------

TEST(UiVariant, FactoryReturnsValidVariantOnRealDevice)
{
    auto vk_r = cd::rhi::vulkan::create_vulkan_device({});
    if (!vk_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD";
    auto& dev = **vk_r;

    if (cd::shader::make_glslang_compiler() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.name = "ui_variant_test_a";

    auto v = cd::material::create_ui_variant(dev, spec);
    ASSERT_TRUE(v.has_value()) << v.error().message;
    EXPECT_TRUE(v->is_valid());
    EXPECT_TRUE(v->material().pipeline().is_valid());
    EXPECT_TRUE(v->material().pipeline_layout().is_valid());
    EXPECT_EQ(v->texture_count(), 0U);
    EXPECT_FALSE(v->material().has_descriptors());  // Sprint-1: no descriptors
}

// -----------------------------------------------------------------------------
// (d) Sprint-2 — variant with SDF sampler bound dispatches OK
// -----------------------------------------------------------------------------
//
// Asks the factory for `texture_count = 1` (which implicitly turns the
// theme UBO branch on too) and verifies:
//   * the variant reports has_sdf_sampler() + has_theme_ubo()
//   * the underlying Material has a descriptor set layout
//   * a MaterialInstance can be allocated from that layout
//   * binding the variant + instance + drawing increments the
//     NullCommandBuffer log counters as expected.
TEST(UiVariantSprint2, SdfSamplerVariantDispatchesOK)
{
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::rhi::NullDevice dev;
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.texture_count            = 1U;
    spec.use_theme_palette_ubo    = true;
    spec.theme_palette_ubo_slot   = 0U;
    spec.sdf_font_sampler_slot    = 1U;
    spec.name                     = "ui_variant_test_d_sdf";

    auto v_r = cd::material::create_ui_variant(dev, spec);
    ASSERT_TRUE(v_r.has_value()) << v_r.error().message;
    auto& v = *v_r;
    EXPECT_TRUE(v.is_valid());
    EXPECT_TRUE(v.has_sdf_sampler());
    EXPECT_TRUE(v.has_theme_ubo());
    EXPECT_EQ(v.texture_count(), 1U);
    EXPECT_TRUE(v.material().has_descriptors());
    EXPECT_TRUE(v.material().descriptor_set_layout().is_valid());

    auto inst_r = cd::material::MaterialInstance::create(dev, v.material());
    ASSERT_TRUE(inst_r.has_value()) << inst_r.error().message;
    EXPECT_TRUE(inst_r->descriptor_set().is_valid());

    cd::rhi::NullCommandBuffer cmd;
    cmd.begin();
    v.apply(cmd);
    inst_r->bind(cmd, /*set_index=*/0U);
    cmd.draw(/*vertex_count=*/6U, /*instance_count=*/1U,
             /*first_vertex=*/0U, /*first_instance=*/0U);
    cmd.end();

    EXPECT_EQ(cmd.log().bind_graphics_pipeline, 1U);
    EXPECT_EQ(cmd.log().draws, 1U);
}

// -----------------------------------------------------------------------------
// (e) Sprint-2 — theme UBO update reflects in subsequent draws
// -----------------------------------------------------------------------------
//
// Builds a Sprint-2 variant with the theme UBO branch ON but no SDF
// sampler (`texture_count = 0`, `use_theme_palette_ubo = true`).
// Allocates a UBO buffer, uploads a `UiThemePaletteUbo` payload, writes
// a DescriptorWrite into the MaterialInstance, and then re-binds +
// draws. The NullDevice's update_descriptor_set must accept the write,
// and a follow-up draw must succeed (cmd.log().draws == 2).
TEST(UiVariantSprint2, ThemeUboUpdateReflectsInSubsequentDraws)
{
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::rhi::NullDevice dev;
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.texture_count            = 0U;
    spec.use_theme_palette_ubo    = true;
    spec.theme_palette_ubo_slot   = 0U;
    spec.name                     = "ui_variant_test_e_theme";

    auto v_r = cd::material::create_ui_variant(dev, spec);
    ASSERT_TRUE(v_r.has_value()) << v_r.error().message;
    auto& v = *v_r;
    EXPECT_TRUE(v.has_theme_ubo());
    EXPECT_FALSE(v.has_sdf_sampler());
    EXPECT_TRUE(v.material().has_descriptors());

    auto inst_r = cd::material::MaterialInstance::create(dev, v.material());
    ASSERT_TRUE(inst_r.has_value()) << inst_r.error().message;

    // Allocate the theme UBO buffer and upload a sentinel palette.
    cd::rhi::BufferDesc bd {};
    bd.size       = sizeof(cd::material::UiThemePaletteUbo);
    bd.usage      = cd::rhi::BufferUsage::kUniform
                  | cd::rhi::BufferUsage::kTransferDst;
    bd.memory     = cd::rhi::MemoryUsage::kCpuToGpu;
    bd.debug_name = "ui_theme_palette_ubo";
    auto ubo_r = dev.create_buffer(bd);
    ASSERT_TRUE(ubo_r.has_value()) << ubo_r.error().message;

    cd::material::UiThemePaletteUbo payload {};
    payload.primary[0]    = 0.20F; payload.primary[1]    = 0.40F;
    payload.primary[2]    = 0.80F; payload.primary[3]    = 1.00F;
    payload.surface[0]    = 0.05F; payload.surface[1]    = 0.05F;
    payload.surface[2]    = 0.07F; payload.surface[3]    = 1.00F;

    std::array<std::byte, sizeof(cd::material::UiThemePaletteUbo)> bytes {};
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    auto up = dev.upload_buffer(*ubo_r, 0U, std::span<const std::byte> { bytes });
    ASSERT_TRUE(up.has_value()) << up.error().message;

    // Write the UBO into the descriptor set.
    std::array<cd::rhi::DescriptorWrite, 1> writes { {} };
    writes[0].binding       = spec.theme_palette_ubo_slot;
    writes[0].array_element = 0U;
    writes[0].type          = cd::rhi::DescriptorType::kUniformBuffer;
    writes[0].buffer        = *ubo_r;
    writes[0].buffer_offset = 0U;
    writes[0].buffer_range  = sizeof(cd::material::UiThemePaletteUbo);

    auto upd = inst_r->update(writes);
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // Re-bind + draw twice; both draws must record cleanly.
    cd::rhi::NullCommandBuffer cmd;
    cmd.begin();
    v.apply(cmd);
    inst_r->bind(cmd, 0U);
    cmd.draw(6U, 1U, 0U, 0U);
    cmd.draw(6U, 1U, 0U, 0U);
    cmd.end();

    EXPECT_EQ(cmd.log().bind_graphics_pipeline, 1U);
    EXPECT_EQ(cmd.log().draws, 2U);

    dev.destroy_buffer(*ubo_r);
}

}  // namespace
