// =============================================================================
// CHROMODYNAMIC -- cd::ui::renderer_rhi material-variant Submitter tests
// Phase 608 / M7 W3 -- Route A wire-up.
//
// Exercises the Route A factory
// (`Submitter::create_with_material_ui_variant`). The factory:
//   * accepts a pre-built `cd::material::UiVariant` (move-in),
//   * allocates the same ring vb/ib pair the plain `create()` factory does,
//   * binds the variant's pipeline + pushes a `vec2 inv_viewport` push
//     constant inside `record()` (matching the variant's vertex shader).
//
// Test (a) prefers a real Vulkan device (so the pipeline state goes
// through a true backend), but GTEST_SKIPs when no Vulkan ICD is present
// so headless CI hosts still link the binary cleanly. The test also
// gracefully skips when the engine is built without CD_ENABLE_GLSLANG,
// because the cd::material UI variant factory needs the glslang backend
// to compile its inline Sprint-1 vertex/fragment shaders.
// =============================================================================
#include <cd/material/UiVariant.hpp>
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

// Try to bring up a real Vulkan device. Returns nullptr if the host has
// no Vulkan ICD (typical for headless CI). Tests gate on this and skip
// gracefully so the binary keeps compiling everywhere.
[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_vulkan()
{
    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.app_name          = "test_submitter_material_route";
    vci.enable_validation = false;  // CI hosts often lack the validation layer
    auto dr = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dr.has_value())
    {
        return nullptr;
    }
    return std::move(*dr);
}

// Build a default-spec UiVariant against `dev`. Returns std::nullopt on
// failure so the caller can ASSERT outside this helper.
[[nodiscard]] cd::core::Result<cm::UiVariant>
make_default_variant(cd::rhi::IDevice& dev)
{
    static constexpr std::array<cd::rhi::Format, 1> kColorFormats {
        cd::rhi::Format::kBGRA8Unorm,
    };
    cm::UiVariantSpec spec {};
    spec.color_attachment_formats = kColorFormats;
    spec.name                     = "submitter_material_route_test_variant";
    return cm::create_ui_variant(dev, spec);
}

}  // namespace

// -----------------------------------------------------------------------------
// (a) -- create_with_material_ui_variant accepts a pre-built UiVariant and
//        the resulting submitter is valid + the inline pipeline path is NOT
//        engaged (Route A owns the pipeline through the variant).
// -----------------------------------------------------------------------------
TEST(SubmitterMaterialRoute, CreateWithMaterialUiVariantProducesValidHandle)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; Route A factory needs a real device "
                        "to exercise the cd::material pipeline create call";
    }
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG; "
                        "cd::material::create_ui_variant needs glslang to "
                        "compile its inline Sprint-1 vertex+fragment shaders";
    }

    auto var_r = make_default_variant(*vk);
    ASSERT_TRUE(var_r.has_value())
        << "cd::material::create_ui_variant failed: domain=" << var_r.error().domain
        << " code=" << var_r.error().code
        << " msg="  << std::string(var_r.error().message);
    EXPECT_TRUE(var_r->is_valid());

    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;

    auto sub_r = rh::Submitter::create_with_material_ui_variant(
        *vk, info, std::move(*var_r));
    ASSERT_TRUE(sub_r.has_value())
        << "create_with_material_ui_variant failed: domain=" << sub_r.error().domain
        << " code=" << sub_r.error().code
        << " msg="  << std::string(sub_r.error().message);
    auto& sub = *sub_r;
    EXPECT_TRUE(sub.is_valid());
    EXPECT_EQ(sub.vertex_count(),  0U);
    EXPECT_EQ(sub.index_count(),   0U);
    EXPECT_EQ(sub.command_count(), 0U);
}

// -----------------------------------------------------------------------------
// (b) -- pushing 4 same-state quads through the batcher merges them into
//        ONE DrawCommand (matches the Route B test contract), and the
//        Route A `record()` path issues that single draw against a real
//        command buffer without crashing the pipeline bind + push_constants
//        + draw_indexed sequence.
// -----------------------------------------------------------------------------
TEST(SubmitterMaterialRoute, RecordEmitsExpectedDrawCallCount)
{
    auto vk = try_make_vulkan();
    if (vk == nullptr)
    {
        GTEST_SKIP() << "no Vulkan ICD; record() path is best validated on the "
                        "real backend (NullDevice draws are no-op)";
    }
    if (cd::shader::make_glslang_compiler() == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    auto var_r = make_default_variant(*vk);
    ASSERT_TRUE(var_r.has_value()) << var_r.error().message;

    rh::SubmitterCreateInfo info {};
    info.max_vertices = 1024U;
    info.max_indices  = 4096U;
    info.color_format = cd::rhi::Format::kBGRA8Unorm;
    auto sub_r = rh::Submitter::create_with_material_ui_variant(
        *vk, info, std::move(*var_r));
    ASSERT_TRUE(sub_r.has_value()) << sub_r.error().message;
    auto& sub = *sub_r;

    ur::DrawBatcher batcher;
    batcher.begin_frame();
    for (int i = 0; i < 4; ++i)
    {
        batcher.quad(static_cast<float>(i * 32), 8.0F, 24.0F, 24.0F,
                     ur::Color::white());
    }

    EXPECT_TRUE(sub.upload(batcher));
    EXPECT_EQ(sub.vertex_count(),  16U);  // 4 quads * 4 verts
    EXPECT_EQ(sub.index_count(),   24U);  // 4 quads * 6 indices
    // All four share the default scissor + kSolid variant -> single
    // batched draw command. This is the count `record()` iterates.
    EXPECT_EQ(sub.command_count(), 1U);

    // Smoke: record into a real Vulkan command buffer. The recording
    // call binds the variant's pipeline + pushes the inv-viewport
    // constant + issues one draw_indexed. We do NOT submit (no
    // swapchain image bound), so this is a pure "the call sequence is
    // recordable" smoke test, exactly matching the Route B inline-shader
    // test contract.
    auto cmd = vk->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    sub.record(*cmd, cd::rhi::Extent2D { 800U, 600U });
    cmd->end();
}
