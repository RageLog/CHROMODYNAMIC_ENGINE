// =============================================================================
// CHROMODYNAMIC -- engine/render/post/tests/composite/
//                 test_composite_ddgi_restir_hook.cpp
// phase691 -- M14 W4 -- Vulkan-gated smoke for the composite DDGI + ReSTIR
//             hook. Verifies that the GI-hook FS variant compiles to SPIR-V
//             via glslang, that a graphics pipeline can be created with BOTH
//             new sampler bindings (DDGI indirect irradiance @ binding 6,
//             ReSTIR denoised direct light @ binding 7), and that the FS
//             can be DISPATCHED into a one-shot command buffer without a
//             single Vulkan validation error.
//
// MOMENT: a graphics dev flips CD_COMPOSITE_USE_DDGI=ON in their CMake,
// recompiles, and the scene gets actual indirect bounce lighting in the
// next frame -- the full GI/RT chain is consumable, not just dispatchable.
//
// Gating: skips at runtime when no Vulkan ICD is available; skips when the
// engine was built without CD_ENABLE_GLSLANG (the only path we have today
// to take the GLSL source string -> SPIR-V).
//
// hello_engine is NOT touched (per FROZEN constraint).
// =============================================================================

#include <cd/post/composite/Composite.hpp>

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_vulkan_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)                                    \
    auto dev_var = try_make_vulkan_device();                          \
    if (!dev_var)                                                     \
        GTEST_SKIP() << "no Vulkan ICD available on this host";

// Allocate an `w x h` sampled texture and a matching view at the given
// format. Used to back the 8 sampled bindings the GI-hook FS reads.
[[nodiscard]] std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
make_sampled(cd::rhi::IDevice& dev,
             std::uint32_t     w,
             std::uint32_t     h,
             cd::rhi::Format   format,
             cd::rhi::TextureUsage usage,
             const char*       debug_name)
{
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = format;
    td.extent       = { w, h, 1U };
    td.mip_levels   = 1U;
    td.array_layers = 1U;
    td.samples      = cd::rhi::SampleCount::k1;
    td.usage        = usage;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    td.debug_name   = debug_name;
    auto tex = dev.create_texture(td);
    if (!tex.has_value())
        return {};
    cd::rhi::TextureViewDesc vd {};
    vd.texture     = *tex;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = format;
    vd.base_mip    = 0U;
    vd.mip_count   = 1U;
    vd.base_layer  = 0U;
    vd.layer_count = 1U;
    auto view = dev.create_texture_view(vd);
    if (!view.has_value())
    {
        dev.destroy_texture(*tex);
        return {};
    }
    return { *tex, *view };
}

}  // namespace

// ---------------------------------------------------------------------------
// Static (no-Vulkan) sanity: the helper splices the macro #define block
// after #version 450 when either flag is set, and returns the source
// unchanged when both flags are false. Locks the "default OFF means
// byte-equivalent baseline" contract the README documents.
// ---------------------------------------------------------------------------
TEST(CompositeDdgiRestirHook, MakeSourceBothOffMatchesBaseline)
{
    const std::string out =
        cd::post::composite::make_composite_fs_source_with_gi_hooks(
            /*use_ddgi=*/false, /*use_restir=*/false);
    EXPECT_EQ(out, std::string(cd::post::composite::kCompositeFSWithGiHooks));
    EXPECT_EQ(out.find("#define CD_COMPOSITE_USE_DDGI"),   std::string::npos);
    EXPECT_EQ(out.find("#define CD_COMPOSITE_USE_RESTIR"), std::string::npos);
}

TEST(CompositeDdgiRestirHook, MakeSourceDdgiOnPrependsMacro)
{
    const std::string out =
        cd::post::composite::make_composite_fs_source_with_gi_hooks(
            /*use_ddgi=*/true, /*use_restir=*/false);
    // #version line must precede the #define line (GLSL requires #version
    // as the first non-whitespace token).
    const auto v_pos = out.find("#version 450");
    const auto m_pos = out.find("#define CD_COMPOSITE_USE_DDGI");
    ASSERT_NE(v_pos, std::string::npos);
    ASSERT_NE(m_pos, std::string::npos);
    EXPECT_LT(v_pos, m_pos);
    EXPECT_EQ(out.find("#define CD_COMPOSITE_USE_RESTIR"), std::string::npos);
}

TEST(CompositeDdgiRestirHook, MakeSourceBothOnPrependsBothMacros)
{
    const std::string out =
        cd::post::composite::make_composite_fs_source_with_gi_hooks(
            /*use_ddgi=*/true, /*use_restir=*/true);
    EXPECT_NE(out.find("#define CD_COMPOSITE_USE_DDGI 1"),   std::string::npos);
    EXPECT_NE(out.find("#define CD_COMPOSITE_USE_RESTIR 1"), std::string::npos);
}

TEST(CompositeDdgiRestirHook, BindingsSlotEnumLayoutFrozen)
{
    // The renderer's CPU bind code keys off these exact slot indices when
    // it wires the DDGI + ReSTIR output views into the composite pass.
    // Drift here would silently mis-bind, so freeze the contract.
    EXPECT_EQ(static_cast<std::uint32_t>(
                  cd::post::composite::BindingSlot::kDdgiIndirectIrradiance),
              6U);
    EXPECT_EQ(static_cast<std::uint32_t>(
                  cd::post::composite::BindingSlot::kRestirDenoisedDirect),
              7U);
    EXPECT_EQ(cd::post::composite::kBindingCountWithGiHooks, 8U);
}

// ---------------------------------------------------------------------------
// Vulkan-gated: compile FS+VS with BOTH macros ON, create a graphics
// pipeline that exposes the 8-binding descriptor set layout, write
// descriptors for all eight bindings + write the push constants + draw
// a single fullscreen triangle into a tiny RGBA8 colour MRT pair.
// Validation layers run with VK_LAYER_KHRONOS_validation -- any binding
// mismatch, uninitialised image layout, or layout/SPIR-V incompatibility
// surfaces as a debug callback that the cd::rhi::vulkan device promotes
// to an error in test builds.
// ---------------------------------------------------------------------------
TEST(CompositeDdgiRestirHook, GiHookFsCompilesAndDispatches)
{
    SKIP_IF_NO_VULKAN(dev);

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG -- cannot "
                        "compile the composite FS GLSL string at runtime.";

    // ---- 1. Compile VS + GI-hook FS to SPIR-V ----------------------------
    cd::shader::CompileDesc vsd {};
    vsd.source      = cd::post::composite::kCompositeVS;
    vsd.stage       = cd::shader::ShaderStage::kVertex;
    vsd.source_name = "cd_composite.vert";
    auto vs_compiled = compiler->compile(vsd);
    ASSERT_TRUE(vs_compiled.has_value()) << vs_compiled.error().message;

    const std::string fs_src =
        cd::post::composite::make_composite_fs_source_with_gi_hooks(
            /*use_ddgi=*/true, /*use_restir=*/true);
    cd::shader::CompileDesc fsd {};
    fsd.source      = fs_src;
    fsd.stage       = cd::shader::ShaderStage::kFragment;
    fsd.source_name = "cd_composite_gi_hooks.frag";
    auto fs_compiled = compiler->compile(fsd);
    ASSERT_TRUE(fs_compiled.has_value()) << fs_compiled.error().message;

    // ---- 2. ShaderModules -------------------------------------------------
    cd::rhi::ShaderModuleDesc vs_smd {};
    vs_smd.stage       = cd::rhi::ShaderStage::kVertex;
    vs_smd.code        = vs_compiled->spirv.data();
    vs_smd.code_size   = vs_compiled->spirv.size() * sizeof(std::uint32_t);
    vs_smd.entry_point = "main";
    vs_smd.debug_name  = "cd_composite_gi_hooks_vs";
    auto vs_mod = dev->create_shader_module(vs_smd);
    ASSERT_TRUE(vs_mod.has_value()) << vs_mod.error().message;

    cd::rhi::ShaderModuleDesc fs_smd {};
    fs_smd.stage       = cd::rhi::ShaderStage::kFragment;
    fs_smd.code        = fs_compiled->spirv.data();
    fs_smd.code_size   = fs_compiled->spirv.size() * sizeof(std::uint32_t);
    fs_smd.entry_point = "main";
    fs_smd.debug_name  = "cd_composite_gi_hooks_fs";
    auto fs_mod = dev->create_shader_module(fs_smd);
    ASSERT_TRUE(fs_mod.has_value()) << fs_mod.error().message;

    // ---- 3. Descriptor set layout: 8 combined-image-sampler bindings ------
    std::array<cd::rhi::DescriptorSetLayoutBinding, 8> bindings {};
    for (std::uint32_t i = 0U; i < bindings.size(); ++i)
    {
        bindings[i].binding = i;
        bindings[i].type    = cd::rhi::DescriptorType::kCombinedImageSampler;
        bindings[i].count   = 1U;
        bindings[i].stages  = cd::rhi::ShaderStage::kFragment;
    }
    cd::rhi::DescriptorSetLayoutDesc dsld {};
    dsld.bindings = bindings;
    auto dsl = dev->create_descriptor_set_layout(dsld);
    ASSERT_TRUE(dsl.has_value()) << dsl.error().message;

    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { *dsl };
    std::array<cd::rhi::PushConstantRange, 1> pcrs {};
    pcrs[0].stages = cd::rhi::ShaderStage::kFragment;
    pcrs[0].offset = 0U;
    pcrs[0].size   = static_cast<std::uint32_t>(sizeof(cd::post::composite::Push));

    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts    = set_layouts;
    pld.push_constants = pcrs;
    auto pl = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(pl.has_value()) << pl.error().message;

    // ---- 4. Graphics pipeline targeting a 2-MRT RGBA8 colour pair --------
    // The composite FS writes BOTH `out_color` (loc 0) and `out_history`
    // (loc 1), so the pipeline must expose two colour-attachment formats
    // or Vulkan validation rejects the bind. Disable depth + blend; the
    // smoke FS is a single fullscreen-triangle write.
    std::array<cd::rhi::Format, 2> color_formats {
        cd::rhi::Format::kRGBA8Unorm,
        cd::rhi::Format::kRGBA8Unorm,
    };
    std::array<cd::rhi::BlendAttachmentState, 2> blend {};
    cd::rhi::DepthStencilState ds {};
    ds.depth_test  = false;
    ds.depth_write = false;
    cd::rhi::RasterState raster {};
    raster.cull = cd::rhi::CullMode::kNone;       // fullscreen triangle
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout                   = *pl;
    gpd.vertex_shader            = *vs_mod;
    gpd.fragment_shader          = *fs_mod;
    gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.color_attachment_formats = color_formats;
    gpd.blend_attachments        = blend;
    gpd.depth_stencil            = ds;
    gpd.raster                   = raster;
    auto gp = dev->create_graphics_pipeline(gpd);
    ASSERT_TRUE(gp.has_value()) << gp.error().message;

    // ---- 5. Allocate the 8 sampled inputs + 2 colour-attachment MRTs -----
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;
    std::array<std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>, 8> taps {};
    static constexpr std::array<const char*, 8> kNames {
        "composite_gi_hook_hdr_color",
        "composite_gi_hook_bloom_mip0",
        "composite_gi_hook_depth",
        "composite_gi_hook_gbuf_normal",
        "composite_gi_hook_history",
        "composite_gi_hook_velocity",
        "composite_gi_hook_ddgi_irr",   // binding 6 — DDGI sample output
        "composite_gi_hook_restir_dir", // binding 7 — ReSTIR denoised output
    };
    for (std::uint32_t i = 0U; i < taps.size(); ++i)
    {
        taps[i] = make_sampled(*dev, kW, kH,
                               cd::rhi::Format::kRGBA16Float,
                               cd::rhi::TextureUsage::kSampled,
                               kNames[i]);
        ASSERT_TRUE(taps[i].first.is_valid())  << "failed: " << kNames[i];
        ASSERT_TRUE(taps[i].second.is_valid()) << "failed: " << kNames[i];
    }
    auto out_color   = make_sampled(*dev, kW, kH,
                                    cd::rhi::Format::kRGBA8Unorm,
                                    cd::rhi::TextureUsage::kColorAttachment |
                                        cd::rhi::TextureUsage::kSampled,
                                    "composite_gi_hook_out_color");
    auto out_history = make_sampled(*dev, kW, kH,
                                    cd::rhi::Format::kRGBA8Unorm,
                                    cd::rhi::TextureUsage::kColorAttachment |
                                        cd::rhi::TextureUsage::kSampled,
                                    "composite_gi_hook_out_history");
    ASSERT_TRUE(out_color.first.is_valid());
    ASSERT_TRUE(out_history.first.is_valid());

    cd::rhi::SamplerDesc sd {};   // defaults: linear, repeat — fine for smoke
    auto smp = dev->create_sampler(sd);
    ASSERT_TRUE(smp.has_value()) << smp.error().message;

    // ---- 6. Allocate descriptor set + write all 8 bindings ---------------
    auto dset = dev->allocate_descriptor_set(*dsl);
    ASSERT_TRUE(dset.has_value()) << dset.error().message;

    std::array<cd::rhi::DescriptorWrite, 8> writes {};
    for (std::uint32_t i = 0U; i < writes.size(); ++i)
    {
        writes[i].binding = i;
        writes[i].type    = cd::rhi::DescriptorType::kCombinedImageSampler;
        writes[i].view    = taps[i].second;
        writes[i].sampler = *smp;
    }
    auto upd = dev->update_descriptor_set(*dset, writes);
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // ---- 7. One-shot command buffer: transitions + draw ------------------
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();

    // Transition every sampled tap UNDEFINED -> kShaderResource so the FS
    // can read them without an in-spec layout error. Colour + history MRTs
    // go to kColorAttachment for the upcoming begin_render_pass.
    std::array<cd::rhi::TextureBarrier, 10> barriers {};
    for (std::uint32_t i = 0U; i < taps.size(); ++i)
    {
        barriers[i].texture = taps[i].first;
        barriers[i].from    = cd::rhi::ResourceState::kUndefined;
        barriers[i].to      = cd::rhi::ResourceState::kShaderResource;
        barriers[i].range   = { 0U, 1U, 0U, 1U };
    }
    barriers[8].texture = out_color.first;
    barriers[8].from    = cd::rhi::ResourceState::kUndefined;
    barriers[8].to      = cd::rhi::ResourceState::kColorAttachment;
    barriers[8].range   = { 0U, 1U, 0U, 1U };
    barriers[9].texture = out_history.first;
    barriers[9].from    = cd::rhi::ResourceState::kUndefined;
    barriers[9].to      = cd::rhi::ResourceState::kColorAttachment;
    barriers[9].range   = { 0U, 1U, 0U, 1U };
    cb->barrier({}, barriers);

    // Begin render pass (dynamic rendering) with the two colour attachments.
    std::array<cd::rhi::ColorAttachmentInfo, 2> color_atts {
        cd::rhi::ColorAttachmentInfo {
            .view        = out_color.second,
            .load_op     = cd::rhi::LoadOp::kClear,
            .store_op    = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } },
        },
        cd::rhi::ColorAttachmentInfo {
            .view        = out_history.second,
            .load_op     = cd::rhi::LoadOp::kClear,
            .store_op    = cd::rhi::StoreOp::kStore,
            .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } },
        },
    };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, { kW, kH } };
    rp.color_attachments = color_atts;
    cb->begin_render_pass(rp);

    cb->set_viewport(cd::rhi::Viewport {
        0.0F, 0.0F, static_cast<float>(kW), static_cast<float>(kH), 0.0F, 1.0F
    });
    cb->set_scissor(cd::rhi::Rect2D { { 0, 0 }, { kW, kH } });
    cb->bind_graphics_pipeline(*gp);
    cb->bind_descriptor_set(0U, *dset);

    // Push constants: all-zero is safe except for the FX exposure floor
    // (the smoke FS multiplies by `max(pc.fx.y, 0.001)`).
    cd::post::composite::Push pc {};
    pc.fx[0] = 0.0F;  // tonemap_op (Narkowicz — ignored by the smoke FS)
    pc.fx[1] = 1.0F;  // exposure mult
    pc.fx[2] = 1.0F;  // sat boost
    pc.fx[3] = 0.0F;  // bloom strength (off so the bloom tap is a no-op)
    cb->push_constants(*pl, cd::rhi::ShaderStage::kFragment,
                       0U,
                       static_cast<std::uint32_t>(sizeof(pc)),
                       &pc);

    // Single fullscreen triangle (3 verts, gl_VertexIndex 0..2 in the VS).
    cb->draw(3U, 1U, 0U, 0U);

    cb->end_render_pass();
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    // ---- 8. Tear down ----------------------------------------------------
    dev->destroy_sampler(*smp);
    dev->destroy_descriptor_set(*dset);
    dev->destroy_graphics_pipeline(*gp);
    dev->destroy_pipeline_layout(*pl);
    dev->destroy_descriptor_set_layout(*dsl);
    dev->destroy_shader_module(*fs_mod);
    dev->destroy_shader_module(*vs_mod);
    for (auto& t : taps)
    {
        dev->destroy_texture_view(t.second);
        dev->destroy_texture(t.first);
    }
    dev->destroy_texture_view(out_color.second);
    dev->destroy_texture(out_color.first);
    dev->destroy_texture_view(out_history.second);
    dev->destroy_texture(out_history.first);
}
