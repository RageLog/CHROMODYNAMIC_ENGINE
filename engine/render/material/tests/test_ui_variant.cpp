// =============================================================================
// CHROMODYNAMIC — cd::material::UiVariant tests
// M3 W1B Sprint-1 (phase 555).
//
// Three cases per the M3 W1B brief:
//   (a) factory returns a valid variant given a real Vulkan device
//       (GTEST_SKIP if no Vulkan ICD or no glslang).
//   (b) variant can be bound to a command buffer without errors
//       (NullDevice + NullCommandBuffer — no Vulkan required for the
//       bind-time contract, exercised against Vulkan in (a)).
//   (c) UiVariantSpec defaults are sensible (texture_count == 0,
//       theme_palette_ubo_binding == -1, blend = alpha, depth disabled).
// =============================================================================
#include <cd/material/UiVariant.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>

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

    // Sprint-1 contract: no texture sampler, no theme UBO.
    EXPECT_EQ(s.texture_count, 0U);
    EXPECT_EQ(s.theme_palette_ubo_binding, -1);

    EXPECT_TRUE(s.color_attachment_formats.empty());  // factory supplies RGBA8 default
    EXPECT_EQ(s.depth_attachment_format, cd::rhi::Format::kUndefined);
    EXPECT_FALSE(s.name.empty());
}

// Sprint-1 guardrails: the factory must reject any spec field that
// belongs to Sprint-2 territory. These are NullDevice-friendly because
// validation happens before any RHI call.
TEST(UiVariantSpec, FactoryRejectsTextureCountForSprint1)
{
    cd::rhi::NullDevice dev;
    cd::material::UiVariantSpec s {};
    s.texture_count = 1U;

    auto v = cd::material::create_ui_variant(dev, s);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

TEST(UiVariantSpec, FactoryRejectsThemeUboBindingForSprint1)
{
    cd::rhi::NullDevice dev;
    cd::material::UiVariantSpec s {};
    s.theme_palette_ubo_binding = 0;

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

}  // namespace
