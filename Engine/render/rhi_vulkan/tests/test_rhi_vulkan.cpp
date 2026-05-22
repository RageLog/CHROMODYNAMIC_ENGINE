// =============================================================================
// CHROMODYNAMIC — cd::rhi_vulkan tests (Sprint S3.3 + S3.4 + S3.5)
//
// All tests gracefully skip when no Vulkan ICD is installed (volkInitialize
// fails). When a driver is present, they exercise real GPU code paths.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>  // complete type for unique_ptr<ICommandBuffer>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi_vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi_vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!dev_var)                     \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

TEST(VulkanDevice, FactoryCreatesOrSkips)
{
    auto r = cd::rhi_vulkan::create_vulkan_device({});
    if (r.has_value())
    {
        EXPECT_EQ((*r)->backend(), cd::rhi::Backend::kVulkan);
        EXPECT_FALSE((*r)->adapter_name().empty());
    }
    else
    {
        GTEST_SKIP() << "no Vulkan ICD available";
    }
}

TEST(VulkanDevice, LimitsArePopulated)
{
    SKIP_IF_NO_VULKAN(dev);
    EXPECT_GT(dev->limits().max_texture_dimension_2d, 0u);
    EXPECT_GT(dev->limits().max_uniform_buffer_range, 0u);
}

TEST(VulkanDevice, HostVisibleBufferRoundTrip)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::BufferDesc desc {};
    desc.size = 256;
    desc.usage = cd::rhi::BufferUsage::kTransferDst | cd::rhi::BufferUsage::kUniform;
    desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf = dev->create_buffer(desc);
    ASSERT_TRUE(buf.has_value()) << buf.error().message;

    std::array<std::byte, 8> payload { std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 },
                                       std::byte { 5 }, std::byte { 6 }, std::byte { 7 }, std::byte { 8 } };
    auto up = dev->upload_buffer(*buf, 64, std::span<const std::byte> { payload });
    EXPECT_TRUE(up.has_value()) << (up.has_value() ? "" : up.error().message);

    dev->destroy_buffer(*buf);
}

TEST(VulkanDevice, GpuOnlyBufferRejectsUpload)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::BufferDesc desc {};
    desc.size = 64;
    desc.usage = cd::rhi::BufferUsage::kStorage;
    desc.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto buf = dev->create_buffer(desc);
    ASSERT_TRUE(buf.has_value());
    std::array<std::byte, 4> payload {};
    auto up = dev->upload_buffer(*buf, 0, std::span<const std::byte> { payload });
    EXPECT_FALSE(up.has_value());
    dev->destroy_buffer(*buf);
}

TEST(VulkanDevice, ZeroSizeBufferRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::BufferDesc desc {};
    desc.size = 0;
    auto r = dev->create_buffer(desc);
    EXPECT_FALSE(r.has_value());
}

TEST(VulkanDevice, ShaderModuleAcceptsValidSpirv)
{
    SKIP_IF_NO_VULKAN(dev);
    // Minimal valid SPIR-V header: magic + version + generator + bound + reserved.
    // We only need vkCreateShaderModule to accept the byte stream; runtime use
    // would need real ops. The validation layer (if loaded) would warn — fine
    // for a creation-only smoke test.
    constexpr std::array<std::uint32_t, 5> spirv {
        0x07230203u,  // SPIR-V magic
        0x00010000u,  // version 1.0
        0x00000000u,  // generator
        1u,           // bound
        0u            // reserved
    };
    cd::rhi::ShaderModuleDesc desc {};
    desc.stage = cd::rhi::ShaderStage::kCompute;
    desc.code = spirv.data();
    desc.code_size = spirv.size() * sizeof(std::uint32_t);
    auto r = dev->create_shader_module(desc);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    dev->destroy_shader_module(*r);
}

TEST(VulkanDevice, ShaderModuleRejectsBadInput)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::ShaderModuleDesc desc {};
    desc.code = nullptr;
    desc.code_size = 0;
    EXPECT_FALSE(dev->create_shader_module(desc).has_value());

    std::array<std::byte, 6> bad {};  // not 4-byte aligned size
    desc.code = bad.data();
    desc.code_size = bad.size();
    EXPECT_FALSE(dev->create_shader_module(desc).has_value());
}

TEST(VulkanDevice, WaitIdleNoCrash)
{
    SKIP_IF_NO_VULKAN(dev);
    dev->wait_idle();
}

// --- S3.4: textures / views / samplers --------------------------------------
TEST(VulkanDevice, Texture2DCreateDestroy)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc desc {};
    desc.type = cd::rhi::TextureType::k2D;
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    desc.extent = { 64, 64, 1 };
    desc.mip_levels = 1;
    desc.array_layers = 1;
    desc.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    desc.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto t = dev->create_texture(desc);
    ASSERT_TRUE(t.has_value()) << t.error().message;
    dev->destroy_texture(*t);
}

TEST(VulkanDevice, TextureZeroExtentRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 0, 0, 1 };
    desc.format = cd::rhi::Format::kRGBA8Unorm;
    EXPECT_FALSE(dev->create_texture(desc).has_value());
}

TEST(VulkanDevice, TextureUndefinedFormatRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc desc {};
    desc.extent = { 32, 32, 1 };
    desc.format = cd::rhi::Format::kUndefined;
    EXPECT_FALSE(dev->create_texture(desc).has_value());
}

TEST(VulkanDevice, TextureViewBindsToTexture)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc tdesc {};
    tdesc.format = cd::rhi::Format::kRGBA8Unorm;
    tdesc.extent = { 32, 32, 1 };
    tdesc.usage = cd::rhi::TextureUsage::kSampled;
    auto t = dev->create_texture(tdesc);
    ASSERT_TRUE(t.has_value());

    cd::rhi::TextureViewDesc vdesc {};
    vdesc.texture = *t;
    vdesc.type = cd::rhi::TextureType::k2D;
    vdesc.format = cd::rhi::Format::kUndefined;  // inherit from texture
    auto v = dev->create_texture_view(vdesc);
    ASSERT_TRUE(v.has_value()) << v.error().message;

    dev->destroy_texture_view(*v);
    dev->destroy_texture(*t);
}

TEST(VulkanDevice, TextureViewOnUnknownTextureRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureViewDesc vdesc {};
    vdesc.texture = cd::rhi::TextureHandle {};
    EXPECT_FALSE(dev->create_texture_view(vdesc).has_value());
}

TEST(VulkanDevice, DepthTextureSupported)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc d {};
    d.format = cd::rhi::Format::kD32Float;
    d.extent = { 64, 64, 1 };
    d.usage = cd::rhi::TextureUsage::kDepthStencilAttachment;
    auto t = dev->create_texture(d);
    ASSERT_TRUE(t.has_value()) << t.error().message;
    dev->destroy_texture(*t);
}

TEST(VulkanDevice, SamplerLinearCreate)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::SamplerDesc s {};
    s.mag_filter = cd::rhi::SamplerFilter::kLinear;
    s.min_filter = cd::rhi::SamplerFilter::kLinear;
    s.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    s.address_u = cd::rhi::SamplerAddressMode::kClampToEdge;
    s.address_v = cd::rhi::SamplerAddressMode::kClampToEdge;
    auto h = dev->create_sampler(s);
    ASSERT_TRUE(h.has_value()) << h.error().message;
    dev->destroy_sampler(*h);
}

TEST(VulkanDevice, SamplerCompareCreate)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::SamplerDesc s {};
    s.compare_enable = true;
    s.compare_op = cd::rhi::CompareOp::kLessEqual;
    auto h = dev->create_sampler(s);
    ASSERT_TRUE(h.has_value());
    dev->destroy_sampler(*h);
}

// --- S3.4: descriptor set layout / pipeline layout / compute pipeline -------
TEST(VulkanDevice, DescriptorSetLayoutCreate)
{
    SKIP_IF_NO_VULKAN(dev);
    std::array<cd::rhi::DescriptorSetLayoutBinding, 2> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                             },
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 1,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment,
                                             },
    };
    cd::rhi::DescriptorSetLayoutDesc desc {};
    desc.bindings = bindings;
    auto h = dev->create_descriptor_set_layout(desc);
    ASSERT_TRUE(h.has_value()) << h.error().message;
    dev->destroy_descriptor_set_layout(*h);
}

TEST(VulkanDevice, EmptyDescriptorSetLayoutOK)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::DescriptorSetLayoutDesc desc {};  // no bindings — valid in Vulkan
    auto h = dev->create_descriptor_set_layout(desc);
    ASSERT_TRUE(h.has_value());
    dev->destroy_descriptor_set_layout(*h);
}

TEST(VulkanDevice, PipelineLayoutAcceptsSetsAndPushConstants)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::DescriptorSetLayoutDesc sld {};
    auto sl = dev->create_descriptor_set_layout(sld);
    ASSERT_TRUE(sl.has_value());

    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { *sl };
    std::array<cd::rhi::PushConstantRange, 1> push {
        cd::rhi::PushConstantRange {
                                    .stages = cd::rhi::ShaderStage::kVertex,
                                    .offset = 0,
                                    .size = 16,
                                    },
    };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    pld.push_constants = push;
    auto pl = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(pl.has_value()) << pl.error().message;

    dev->destroy_pipeline_layout(*pl);
    dev->destroy_descriptor_set_layout(*sl);
}

TEST(VulkanDevice, PipelineLayoutRejectsUnknownSetLayout)
{
    SKIP_IF_NO_VULKAN(dev);
    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { cd::rhi::DescriptorSetLayoutHandle {} };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    EXPECT_FALSE(dev->create_pipeline_layout(pld).has_value());
}

TEST(VulkanDevice, ComputePipelineFromShader)
{
    SKIP_IF_NO_VULKAN(dev);
    // Minimal valid SPIR-V header is accepted by the driver for shader-module
    // creation but isn't a runnable compute kernel. We exercise creation only;
    // pipeline creation requires the module to have a valid OpEntryPoint, so we
    // skip the full create_compute_pipeline if the driver rejects the stub.
    constexpr std::array<std::uint32_t, 5> spirv { 0x07230203u, 0x00010000u, 0x00000000u, 1u, 0u };
    cd::rhi::ShaderModuleDesc shdr {};
    shdr.stage = cd::rhi::ShaderStage::kCompute;
    shdr.code = spirv.data();
    shdr.code_size = spirv.size() * sizeof(std::uint32_t);
    auto sm = dev->create_shader_module(shdr);
    ASSERT_TRUE(sm.has_value());

    cd::rhi::DescriptorSetLayoutDesc sld {};
    auto sl = dev->create_descriptor_set_layout(sld);
    ASSERT_TRUE(sl.has_value());

    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { *sl };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto pl = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(pl.has_value());

    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = *pl;
    cpd.shader = *sm;
    auto cp = dev->create_compute_pipeline(cpd);
    // The stub SPIR-V has no entry point; the driver will likely reject. Accept
    // either outcome — but the API surface and handle lifecycle are exercised.
    if (cp.has_value())
    {
        dev->destroy_compute_pipeline(*cp);
    }

    dev->destroy_pipeline_layout(*pl);
    dev->destroy_descriptor_set_layout(*sl);
    dev->destroy_shader_module(*sm);
}

TEST(VulkanDevice, ComputePipelineMissingShaderRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::ComputePipelineDesc cpd {};
    EXPECT_FALSE(dev->create_compute_pipeline(cpd).has_value());
}

TEST(VulkanDevice, GraphicsPipelineRequiresVertexShader)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::GraphicsPipelineDesc gpd {};
    // No vertex_shader / no layout → early rejection.
    auto bad = dev->create_graphics_pipeline(gpd);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(VulkanDevice, GraphicsPipelineRequiresLayout)
{
    SKIP_IF_NO_VULKAN(dev);
    // Provide a vertex shader but no layout → second rejection path.
    constexpr std::array<std::uint32_t, 5> spirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };
    cd::rhi::ShaderModuleDesc sd {};
    sd.stage = cd::rhi::ShaderStage::kVertex;
    sd.code = spirv.data();
    sd.code_size = spirv.size() * sizeof(std::uint32_t);
    auto vs = dev->create_shader_module(sd);
    ASSERT_TRUE(vs.has_value());

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.vertex_shader = *vs;
    auto bad = dev->create_graphics_pipeline(gpd);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    dev->destroy_shader_module(*vs);
}

// --- S3.4: command pool + command buffer + submit ---------------------------
TEST(VulkanDevice, CommandBufferAllocateRecordSubmit)
{
    SKIP_IF_NO_VULKAN(dev);
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);

    cb->begin();
    cb->set_viewport(cd::rhi::Viewport { 0.0F, 0.0F, 64.0F, 64.0F, 0.0F, 1.0F });
    cb->set_scissor(
        cd::rhi::Rect2D {
            { 0,  0  },
            { 64, 64 }
    }
    );
    cb->end();

    // Submit a no-op buffer; wait_idle observes the submission before the
    // command buffer's dtor frees it back to the pool.
    dev->submit(*cb);
    dev->wait_idle();
}

TEST(VulkanDevice, EmptyCommandBufferSubmits)
{
    SKIP_IF_NO_VULKAN(dev);
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
}

// --- S3.4 follow-up: descriptor set allocation -----------------------------
TEST(VulkanDevice, AllocateDescriptorSetFromLayout)
{
    SKIP_IF_NO_VULKAN(dev);
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex,
                                             },
    };
    cd::rhi::DescriptorSetLayoutDesc desc {};
    desc.bindings = bindings;
    auto layout = dev->create_descriptor_set_layout(desc);
    ASSERT_TRUE(layout.has_value());

    auto set = dev->allocate_descriptor_set(*layout);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    dev->destroy_descriptor_set(*set);
    dev->destroy_descriptor_set_layout(*layout);
}

TEST(VulkanDevice, AllocateDescriptorSetUnknownLayoutRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto bad = dev->allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle {});
    EXPECT_FALSE(bad.has_value());
}

// --- S3.4 follow-up: update_descriptor_set ---------------------------------
TEST(VulkanDevice, UpdateDescriptorSetUniformBuffer)
{
    SKIP_IF_NO_VULKAN(dev);

    // Layout with a single UBO binding.
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex,
                                             },
    };
    cd::rhi::DescriptorSetLayoutDesc layout_desc {};
    layout_desc.bindings = bindings;
    auto layout = dev->create_descriptor_set_layout(layout_desc);
    ASSERT_TRUE(layout.has_value());
    auto set = dev->allocate_descriptor_set(*layout);
    ASSERT_TRUE(set.has_value());

    // A host-visible UBO to bind.
    cd::rhi::BufferDesc bd {};
    bd.size = 256;
    bd.usage = cd::rhi::BufferUsage::kUniform;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf = dev->create_buffer(bd);
    ASSERT_TRUE(buf.has_value());

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite {
                                  .binding = 0,
                                  .array_element = 0,
                                  .type = cd::rhi::DescriptorType::kUniformBuffer,
                                  .buffer = *buf,
                                  .buffer_offset = 0,
                                  .buffer_range = 0,  // whole buffer
        },
    };
    auto ok = dev->update_descriptor_set(*set, writes);
    EXPECT_TRUE(ok.has_value()) << (ok.has_value() ? "" : ok.error().message);

    dev->destroy_buffer(*buf);
    dev->destroy_descriptor_set(*set);
    dev->destroy_descriptor_set_layout(*layout);
}

TEST(VulkanDevice, UpdateDescriptorSetCombinedImageSampler)
{
    SKIP_IF_NO_VULKAN(dev);

    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment,
                                             },
    };
    cd::rhi::DescriptorSetLayoutDesc layout_desc {};
    layout_desc.bindings = bindings;
    auto layout = dev->create_descriptor_set_layout(layout_desc);
    ASSERT_TRUE(layout.has_value());
    auto set = dev->allocate_descriptor_set(*layout);
    ASSERT_TRUE(set.has_value());

    cd::rhi::TextureDesc td {};
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { 16, 16, 1 };
    td.usage = cd::rhi::TextureUsage::kSampled;
    auto tex = dev->create_texture(td);
    ASSERT_TRUE(tex.has_value());

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *tex;
    vd.type = cd::rhi::TextureType::k2D;
    auto view = dev->create_texture_view(vd);
    ASSERT_TRUE(view.has_value());

    cd::rhi::SamplerDesc sd {};
    auto smp = dev->create_sampler(sd);
    ASSERT_TRUE(smp.has_value());

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite {
                                  .binding = 0,
                                  .array_element = 0,
                                  .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                  .view = *view,
                                  .sampler = *smp,
                                  },
    };
    auto ok = dev->update_descriptor_set(*set, writes);
    EXPECT_TRUE(ok.has_value()) << (ok.has_value() ? "" : ok.error().message);

    dev->destroy_sampler(*smp);
    dev->destroy_texture_view(*view);
    dev->destroy_texture(*tex);
    dev->destroy_descriptor_set(*set);
    dev->destroy_descriptor_set_layout(*layout);
}

TEST(VulkanDevice, UpdateDescriptorSetUnknownSetRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    std::array<cd::rhi::DescriptorWrite, 0> empty {};
    auto bad = dev->update_descriptor_set(cd::rhi::DescriptorSetHandle {}, empty);
    EXPECT_FALSE(bad.has_value());
}

TEST(VulkanDevice, UpdateDescriptorSetEmptyWritesOK)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::DescriptorSetLayoutDesc ld {};
    auto layout = dev->create_descriptor_set_layout(ld);
    ASSERT_TRUE(layout.has_value());
    auto set = dev->allocate_descriptor_set(*layout);
    ASSERT_TRUE(set.has_value());
    std::array<cd::rhi::DescriptorWrite, 0> empty {};
    EXPECT_TRUE(dev->update_descriptor_set(*set, empty).has_value());
    dev->destroy_descriptor_set(*set);
    dev->destroy_descriptor_set_layout(*layout);
}

// --- S3.4 follow-up: barrier + copy_buffer resource resolution -------------
TEST(VulkanDevice, CommandBufferCopyBufferRealHandles)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::BufferDesc src_desc {};
    src_desc.size = 64;
    src_desc.usage = cd::rhi::BufferUsage::kTransferSrc;
    src_desc.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto src = dev->create_buffer(src_desc);
    ASSERT_TRUE(src.has_value());

    cd::rhi::BufferDesc dst_desc {};
    dst_desc.size = 64;
    dst_desc.usage = cd::rhi::BufferUsage::kTransferDst;
    dst_desc.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto dst = dev->create_buffer(dst_desc);
    ASSERT_TRUE(dst.has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();

    std::array<cd::rhi::BufferCopyRegion, 1> regions {
        cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = 64 },
    };
    cb->copy_buffer(*src, *dst, regions);
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_buffer(*src);
    dev->destroy_buffer(*dst);
}

TEST(VulkanDevice, CommandBufferBarrierTextureRealHandle)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::TextureDesc td {};
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { 32, 32, 1 };
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    auto tex = dev->create_texture(td);
    ASSERT_TRUE(tex.has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();

    // Transition Undefined → TransferDst (typical first-upload barrier).
    std::array<cd::rhi::TextureBarrier, 1> tbs {
        cd::rhi::TextureBarrier {
                                 .texture = *tex,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kTransferDst,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 },
    };
    cb->barrier({}, tbs);
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_texture(*tex);
}

TEST(VulkanDevice, CommandBufferCopyBufferUnknownHandleSilent)
{
    SKIP_IF_NO_VULKAN(dev);
    // copy_buffer with an unknown handle is a caller bug; the recorder must
    // skip silently (not crash the driver). The recorded buffer should still
    // submit cleanly.
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    std::array<cd::rhi::BufferCopyRegion, 1> regions {
        cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = 64 },
    };
    cb->copy_buffer(cd::rhi::BufferHandle {}, cd::rhi::BufferHandle {}, regions);
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
}

// --- S3.5: swapchain ------------------------------------------------------
TEST(VulkanDevice, SwapchainRejectsNullWindow)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::SwapchainDesc desc {};
    desc.extent = { 800, 600 };
    desc.format = cd::rhi::Format::kBGRA8Srgb;
    auto bad = dev->create_swapchain(desc);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(VulkanDevice, SwapchainRejectsZeroExtent)
{
    SKIP_IF_NO_VULKAN(dev);
    // Sentinel non-null pointer (address of a static); the recorder rejects
    // zero-extent before it ever dereferences the handle, so the value doesn't
    // need to be a real window.
    static int sentinel = 0;
    cd::rhi::SwapchainDesc desc {};
    desc.window_handle = &sentinel;
    desc.extent = { 0, 0 };
    auto bad = dev->create_swapchain(desc);
    ASSERT_FALSE(bad.has_value());
}

// --- S3.5 sync primitives ------------------------------------------------
TEST(VulkanDevice, SemaphoreCreateDestroy)
{
    SKIP_IF_NO_VULKAN(dev);
    auto s = dev->create_semaphore();
    ASSERT_TRUE(s.has_value()) << s.error().message;
    dev->destroy_semaphore(*s);
}

TEST(VulkanDevice, FenceSignaledInitiallySignaled)
{
    SKIP_IF_NO_VULKAN(dev);
    auto f = dev->create_fence(true);
    ASSERT_TRUE(f.has_value()) << f.error().message;
    EXPECT_TRUE(dev->is_fence_signaled(*f));
    // wait_for_fence on an already-signaled fence returns immediately.
    EXPECT_TRUE(dev->wait_for_fence(*f, 0).has_value());
    dev->destroy_fence(*f);
}

TEST(VulkanDevice, FenceUnsignaledThenReset)
{
    SKIP_IF_NO_VULKAN(dev);
    auto f = dev->create_fence(/*signaled=*/false);
    ASSERT_TRUE(f.has_value());
    EXPECT_FALSE(dev->is_fence_signaled(*f));
    // wait on an unsignaled fence with timeout=0 should time out.
    auto wait_res = dev->wait_for_fence(*f, 0);
    ASSERT_FALSE(wait_res.has_value());
    EXPECT_EQ(wait_res.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kTimeout));
    dev->reset_fence(*f);  // no-op on already-unsignaled
    dev->destroy_fence(*f);
}

TEST(VulkanDevice, FenceUnknownHandleWaitRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto bad = dev->wait_for_fence(cd::rhi::FenceHandle {}, 0);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(VulkanDevice, TimelineSemaphoreInitialValue)
{
    SKIP_IF_NO_VULKAN(dev);
    auto r = dev->create_timeline_semaphore(42);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(dev->timeline_semaphore_value(*r), 42U);
    dev->destroy_timeline_semaphore(*r);
}

TEST(VulkanDevice, TimelineSemaphoreHostSignalAdvancesValue)
{
    SKIP_IF_NO_VULKAN(dev);
    auto r = dev->create_timeline_semaphore(0);
    ASSERT_TRUE(r.has_value());
    auto sig = dev->signal_timeline_semaphore(*r, 7);
    ASSERT_TRUE(sig.has_value());
    EXPECT_EQ(dev->timeline_semaphore_value(*r), 7U);
    dev->destroy_timeline_semaphore(*r);
}

TEST(VulkanDevice, TimelineSemaphoreSignalNonMonotonicRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto r = dev->create_timeline_semaphore(10);
    ASSERT_TRUE(r.has_value());
    // Equal-or-lesser must fail (timelines are strictly monotonic).
    auto bad = dev->signal_timeline_semaphore(*r, 10);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
    dev->destroy_timeline_semaphore(*r);
}

TEST(VulkanDevice, TimelineSemaphoreHostWaitOnSignaledValueImmediate)
{
    SKIP_IF_NO_VULKAN(dev);
    auto r = dev->create_timeline_semaphore(0);
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(dev->signal_timeline_semaphore(*r, 5).has_value());
    // Already at >= 5, so wait should return immediately.
    auto w = dev->wait_timeline_semaphore(*r, 5, /*timeout_ns=*/0);
    EXPECT_TRUE(w.has_value());
    dev->destroy_timeline_semaphore(*r);
}

TEST(VulkanDevice, TimelineSemaphoreHostWaitOnUnreachedTimesOut)
{
    SKIP_IF_NO_VULKAN(dev);
    auto r = dev->create_timeline_semaphore(0);
    ASSERT_TRUE(r.has_value());
    // Counter is 0, asking for 100 with a zero deadline must time out.
    auto w = dev->wait_timeline_semaphore(*r, 100, /*timeout_ns=*/0);
    ASSERT_FALSE(w.has_value());
    EXPECT_EQ(w.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kTimeout));
    dev->destroy_timeline_semaphore(*r);
}

TEST(VulkanDevice, TimelineSemaphoreUnknownHandleRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto bad = dev->wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle {}, 1, 0);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
    EXPECT_EQ(dev->timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle {}), 0U);
}

// --- S3.6: dynamic-rendering record path -----------------------------------
TEST(VulkanDevice, CommandBufferBeginEndRenderingDynamic)
{
    SKIP_IF_NO_VULKAN(dev);
    // Create a 2D color texture + view to use as the (only) attachment.
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { 64, 64, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kColorAttachment;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex = dev->create_texture(td);
    ASSERT_TRUE(tex.has_value());

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *tex;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = cd::rhi::Format::kRGBA8Unorm;
    vd.base_mip = 0;
    vd.mip_count = 1;
    vd.base_layer = 0;
    vd.layer_count = 1;
    auto view = dev->create_texture_view(vd);
    ASSERT_TRUE(view.has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    // Transition undefined → color attachment so vkCmdBeginRendering is in-spec.
    std::array<cd::rhi::TextureBarrier, 1> tbarriers {
        cd::rhi::TextureBarrier {
                                 .texture = *tex,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cb->barrier({}, tbarriers);

    std::array<cd::rhi::ColorAttachmentInfo, 1> color_attachments {
        cd::rhi::ColorAttachmentInfo {
                                      .view = *view,
                                      .load_op = cd::rhi::LoadOp::kClear,
                                      .store_op = cd::rhi::StoreOp::kStore,
                                      .clear_color = { .f32 = { 0.1F, 0.2F, 0.3F, 1.0F } },
                                      },
    };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area = cd::rhi::Rect2D {
        { 0,  0  },
        { 64, 64 }
    };
    rp.color_attachments = color_attachments;
    cb->begin_render_pass(rp);
    cb->set_viewport(cd::rhi::Viewport { 0.0F, 0.0F, 64.0F, 64.0F, 0.0F, 1.0F });
    cb->set_scissor(
        cd::rhi::Rect2D {
            { 0,  0  },
            { 64, 64 }
    }
    );
    cb->end_render_pass();
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_texture_view(*view);
    dev->destroy_texture(*tex);
}

TEST(VulkanDevice, CommandBufferBindVertexAndIndexBuffers)
{
    SKIP_IF_NO_VULKAN(dev);
    cd::rhi::BufferDesc vbd {};
    vbd.size = 64;
    vbd.usage = cd::rhi::BufferUsage::kVertex;
    vbd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto vb = dev->create_buffer(vbd);
    ASSERT_TRUE(vb.has_value());

    cd::rhi::BufferDesc ibd {};
    ibd.size = 64;
    ibd.usage = cd::rhi::BufferUsage::kIndex;
    ibd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto ib = dev->create_buffer(ibd);
    ASSERT_TRUE(ib.has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    // No render pass needed for these calls — Vulkan accepts vertex/index
    // buffer binds outside a render pass; they only matter at draw time.
    cb->bind_vertex_buffer(0, *vb, 0);
    cb->bind_index_buffer(*ib, 0, cd::rhi::IndexType::kUInt32);
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_buffer(*ib);
    dev->destroy_buffer(*vb);
}

TEST(VulkanDevice, SubmitDescSignalsTimelineSemaphore)
{
    SKIP_IF_NO_VULKAN(dev);
    // End-to-end submit: queue advances the timeline value, host waits.
    auto ts = dev->create_timeline_semaphore(0);
    ASSERT_TRUE(ts.has_value());
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    cb->end();

    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cb.get() };
    std::array<cd::rhi::TimelineSemaphoreSubmit, 1> sig {
        cd::rhi::TimelineSemaphoreSubmit { .semaphore = *ts, .value = 1 },
    };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = cbs;
    sd.signal_timeline_semaphores = sig;
    auto r = dev->submit(sd);
    ASSERT_TRUE(r.has_value());

    // Host wait for value 1 — should complete promptly once GPU is done.
    auto w = dev->wait_timeline_semaphore(*ts, 1, /*timeout_ns=*/static_cast<std::uint64_t>(-1));
    EXPECT_TRUE(w.has_value());
    EXPECT_GE(dev->timeline_semaphore_value(*ts), 1U);
    dev->destroy_timeline_semaphore(*ts);
}

TEST(VulkanDevice, SubmitDescUnknownTimelineRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    cb->end();
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cb.get() };
    std::array<cd::rhi::TimelineSemaphoreSubmit, 1> sig {
        cd::rhi::TimelineSemaphoreSubmit { .semaphore = cd::rhi::TimelineSemaphoreHandle {}, .value = 1 },
    };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = cbs;
    sd.signal_timeline_semaphores = sig;
    auto r = dev->submit(sd);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(VulkanDevice, SubmitDescSignalsFence)
{
    SKIP_IF_NO_VULKAN(dev);
    auto fence = dev->create_fence(/*signaled=*/false);
    ASSERT_TRUE(fence.has_value());
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    cb->end();

    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cb.get() };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = cbs;
    sd.signal_fence = *fence;
    auto r = dev->submit(sd);
    ASSERT_TRUE(r.has_value());
    auto w = dev->wait_for_fence(*fence, static_cast<std::uint64_t>(-1));
    EXPECT_TRUE(w.has_value());
    EXPECT_TRUE(dev->is_fence_signaled(*fence));
    dev->destroy_fence(*fence);
}

// End-to-end shader pipeline: compile GLSL → SPIR-V → ShaderModule →
// GraphicsPipeline. Proves the cd::shader + cd::rhi handoff works on the
// real GPU. The pipeline is created with dynamic_rendering targeting a
// single RGBA8 color attachment — no actual draw is recorded (we exercise
// the shader compilation → pipeline creation path only).
TEST(VulkanDevice, GraphicsPipelineFromCompiledGlslTriangle)
{
    SKIP_IF_NO_VULKAN(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    constexpr const char* kTriVS = R"glsl(
#version 450
const vec2 P[3] = vec2[](vec2(0.0,-0.5),vec2(0.5,0.5),vec2(-0.5,0.5));
void main() { gl_Position = vec4(P[gl_VertexIndex], 0.0, 1.0); }
)glsl";
    constexpr const char* kTriFS = R"glsl(
#version 450
layout(location = 0) out vec4 c;
void main() { c = vec4(1.0, 0.5, 0.2, 1.0); }
)glsl";

    cd::shader::CompileDesc vd {};
    vd.source = kTriVS;
    vd.stage = cd::shader::ShaderStage::kVertex;
    vd.source_name = "triangle.vert";
    auto vs = compiler->compile(vd);
    ASSERT_TRUE(vs.has_value()) << vs.error().message;

    cd::shader::CompileDesc fd {};
    fd.source = kTriFS;
    fd.stage = cd::shader::ShaderStage::kFragment;
    fd.source_name = "triangle.frag";
    auto fs = compiler->compile(fd);
    ASSERT_TRUE(fs.has_value()) << fs.error().message;

    cd::rhi::ShaderModuleDesc vs_desc {};
    vs_desc.stage = cd::rhi::ShaderStage::kVertex;
    vs_desc.code = vs->spirv.data();
    vs_desc.code_size = vs->spirv.size() * sizeof(std::uint32_t);
    auto vs_mod = dev->create_shader_module(vs_desc);
    ASSERT_TRUE(vs_mod.has_value());

    cd::rhi::ShaderModuleDesc fs_desc {};
    fs_desc.stage = cd::rhi::ShaderStage::kFragment;
    fs_desc.code = fs->spirv.data();
    fs_desc.code_size = fs->spirv.size() * sizeof(std::uint32_t);
    auto fs_mod = dev->create_shader_module(fs_desc);
    ASSERT_TRUE(fs_mod.has_value());

    cd::rhi::PipelineLayoutDesc pld {};
    auto pl = dev->create_pipeline_layout(pld);
    ASSERT_TRUE(pl.has_value());

    std::array<cd::rhi::Format, 1> color_formats { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.vertex_shader = *vs_mod;
    gpd.fragment_shader = *fs_mod;
    gpd.layout = *pl;
    gpd.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.color_attachment_formats = color_formats;
    auto gp = dev->create_graphics_pipeline(gpd);
    ASSERT_TRUE(gp.has_value()) << gp.error().message;

    dev->destroy_graphics_pipeline(*gp);
    dev->destroy_pipeline_layout(*pl);
    dev->destroy_shader_module(*fs_mod);
    dev->destroy_shader_module(*vs_mod);
}

TEST(VulkanDevice, CommandBufferBindUnknownPipelineIsSilent)
{
    // Defensive contract: invalid handles must not crash the recorder. The
    // command buffer silently skips the vkCmdBindPipeline call.
    SKIP_IF_NO_VULKAN(dev);
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    cb->bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle {});
    cb->bind_compute_pipeline(cd::rhi::ComputePipelineHandle {});
    cb->bind_descriptor_set(0, cd::rhi::DescriptorSetHandle {});
    cb->bind_vertex_buffer(0, cd::rhi::BufferHandle {}, 0);
    cb->bind_index_buffer(cd::rhi::BufferHandle {}, 0, cd::rhi::IndexType::kUInt16);
    std::array<std::uint32_t, 4> pc { 1, 2, 3, 4 };
    cb->push_constants(
        cd::rhi::PipelineLayoutHandle {},
        cd::rhi::ShaderStage::kVertex,
        0,
        static_cast<std::uint32_t>(pc.size() * sizeof(std::uint32_t)),
        pc.data()
    );
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
}

#if defined(_WIN32)
namespace
{
class HiddenWin32Window
{
public:
    HiddenWin32Window()
    {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance_;
        wc.lpszClassName = L"CdRhiVulkanTestWindow";
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(
            0,
            wc.lpszClassName,
            L"cd_rhi_vulkan_test",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            256,
            256,
            nullptr,
            nullptr,
            instance_,
            nullptr
        );
    }

    ~HiddenWin32Window()
    {
        if (hwnd_ != nullptr)
            DestroyWindow(hwnd_);
        UnregisterClassW(L"CdRhiVulkanTestWindow", instance_);
    }

    HiddenWin32Window(const HiddenWin32Window&) = delete;
    HiddenWin32Window& operator=(const HiddenWin32Window&) = delete;
    HiddenWin32Window(HiddenWin32Window&&) = delete;
    HiddenWin32Window& operator=(HiddenWin32Window&&) = delete;

    [[nodiscard]] HWND hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] HINSTANCE hinstance() const noexcept
    {
        return instance_;
    }

private:
    HINSTANCE instance_ { nullptr };
    HWND hwnd_ { nullptr };
};
}  // namespace

TEST(VulkanDevice, SwapchainCreateAndDestroyWin32)
{
    SKIP_IF_NO_VULKAN(dev);
    HiddenWin32Window win;
    if (win.hwnd() == nullptr)
    {
        GTEST_SKIP() << "could not create test HWND";
    }
    cd::rhi::SwapchainDesc desc {};
    desc.window_handle = win.hwnd();
    desc.display_handle = win.hinstance();
    desc.extent = { 256, 256 };
    desc.image_count = 2;
    desc.format = cd::rhi::Format::kBGRA8Srgb;
    desc.vsync = true;

    auto sc = dev->create_swapchain(desc);
    ASSERT_TRUE(sc.has_value()) << sc.error().message;
    EXPECT_GE(dev->swapchain_image_count(*sc), 2U);
    dev->wait_idle();
    dev->destroy_swapchain(*sc);
}

TEST(VulkanDevice, SwapchainAcquireImageWithFenceWin32)
{
    SKIP_IF_NO_VULKAN(dev);
    HiddenWin32Window win;
    if (win.hwnd() == nullptr)
    {
        GTEST_SKIP() << "could not create test HWND";
    }
    cd::rhi::SwapchainDesc desc {};
    desc.window_handle = win.hwnd();
    desc.display_handle = win.hinstance();
    desc.extent = { 256, 256 };
    desc.image_count = 2;
    desc.format = cd::rhi::Format::kBGRA8Srgb;
    desc.vsync = true;
    auto sc = dev->create_swapchain(desc);
    ASSERT_TRUE(sc.has_value());

    // Acquire with a fence we can wait on. No semaphore (binary semaphores need
    // a paired submit to consume the signal; for a creation-shape test we want
    // a CPU sync point only).
    auto fence = dev->create_fence(/*signaled=*/false);
    ASSERT_TRUE(fence.has_value());
    auto idx = dev->acquire_next_image(*sc, cd::rhi::SemaphoreHandle {}, *fence, /*timeout_ns=*/UINT64_MAX);
    ASSERT_TRUE(idx.has_value()) << idx.error().message;
    EXPECT_LT(*idx, dev->swapchain_image_count(*sc));

    // Wait until acquire has completed on the CPU.
    ASSERT_TRUE(dev->wait_for_fence(*fence, UINT64_MAX).has_value());

    dev->destroy_fence(*fence);
    // No present: the image was acquired but never transitioned to PRESENT_SRC
    // (would require submitting a barrier-only command buffer). Driver-safe.
    dev->wait_idle();
    dev->destroy_swapchain(*sc);
}

TEST(VulkanDevice, SwapchainAcquireUnknownRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto bad =
        dev->acquire_next_image(cd::rhi::SwapchainHandle {}, cd::rhi::SemaphoreHandle {}, cd::rhi::FenceHandle {}, 0);
    ASSERT_FALSE(bad.has_value());
}

TEST(VulkanDevice, SwapchainPresentUnknownRejected)
{
    SKIP_IF_NO_VULKAN(dev);
    auto bad = dev->present(cd::rhi::SwapchainHandle {}, 0, {});
    ASSERT_FALSE(bad.has_value());
}

// Full per-frame loop: acquire → barrier(undef→color) → clear → barrier
// (color→present) → submit (signal present sem) → present. The first time
// the swapchain image is used the layout is UNDEFINED; barrier() upgrades
// it. After rendering we transition to PRESENT_SRC and hand off to the
// driver.
TEST(VulkanDevice, SwapchainFullPresentLoopWin32)
{
    SKIP_IF_NO_VULKAN(dev);
    HiddenWin32Window win;
    ASSERT_NE(win.hwnd(), nullptr);

    cd::rhi::SwapchainDesc sd {};
    sd.window_handle = win.hwnd();
    sd.display_handle = win.hinstance();
    sd.extent = { 128, 96 };
    sd.image_count = 2;
    sd.format = cd::rhi::Format::kBGRA8Unorm;
    sd.vsync = true;
    auto sc = dev->create_swapchain(sd);
    ASSERT_TRUE(sc.has_value()) << "create_swapchain failed: " << sc.error().message;

    auto acquire_sem = dev->create_semaphore();
    ASSERT_TRUE(acquire_sem.has_value());
    auto present_sem = dev->create_semaphore();
    ASSERT_TRUE(present_sem.has_value());
    auto frame_fence = dev->create_fence(/*signaled=*/false);
    ASSERT_TRUE(frame_fence.has_value());

    auto idx = dev->acquire_next_image(*sc, *acquire_sem, cd::rhi::FenceHandle {}, static_cast<std::uint64_t>(-1));
    ASSERT_TRUE(idx.has_value());

    // Resolve the swapchain image as a TextureHandle so barrier() can use it.
    const auto sc_image = dev->swapchain_image(*sc, *idx);
    ASSERT_TRUE(sc_image.is_valid());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    // UNDEFINED → COLOR_ATTACHMENT.
    std::array<cd::rhi::TextureBarrier, 1> to_color {
        cd::rhi::TextureBarrier {
                                 .texture = sc_image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cb->barrier({}, to_color);
    // COLOR_ATTACHMENT → PRESENT (no actual draw; the clear during begin_render_pass
    // would require an image view, which we skip to keep this test minimal —
    // we exercise the barrier+sync hand-off path).
    std::array<cd::rhi::TextureBarrier, 1> to_present {
        cd::rhi::TextureBarrier {
                                 .texture = sc_image,
                                 .from = cd::rhi::ResourceState::kColorAttachment,
                                 .to = cd::rhi::ResourceState::kPresent,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cb->barrier({}, to_present);
    cb->end();

    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cb.get() };
    std::array<cd::rhi::SemaphoreSubmit, 1> waits { cd::rhi::SemaphoreSubmit { .semaphore = *acquire_sem } };
    std::array<cd::rhi::SemaphoreSubmit, 1> sigs { cd::rhi::SemaphoreSubmit { .semaphore = *present_sem } };
    cd::rhi::SubmitDesc submit {};
    submit.command_buffers = cbs;
    submit.wait_semaphores = waits;
    submit.signal_semaphores = sigs;
    submit.signal_fence = *frame_fence;
    auto sr = dev->submit(submit);
    ASSERT_TRUE(sr.has_value()) << "submit failed: " << sr.error().message;

    std::array<cd::rhi::SemaphoreHandle, 1> present_waits { *present_sem };
    auto pr = dev->present(*sc, *idx, present_waits);
    ASSERT_TRUE(pr.has_value()) << "present failed: " << pr.error().message;

    // Block until the frame finished; the dtor must not race with in-flight work.
    ASSERT_TRUE(dev->wait_for_fence(*frame_fence, static_cast<std::uint64_t>(-1)).has_value());
    dev->wait_idle();

    dev->destroy_fence(*frame_fence);
    dev->destroy_semaphore(*present_sem);
    dev->destroy_semaphore(*acquire_sem);
    dev->destroy_swapchain(*sc);
}
#endif  // _WIN32

}  // namespace
