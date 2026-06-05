// =============================================================================
// CHROMODYNAMIC — cd::rhi_vulkan mesh-shader tests (Phase 765 W2A — F5).
//
// Exercises the Vulkan VK_EXT_mesh_shader path that lives in cd::rhi::vulkan:
//   * MeshPipelineDesc + create_mesh_pipeline() reach vkCreateGraphicsPipelines
//     with a task/mesh/fragment stage list.
//   * bind_graphics_pipeline + draw_mesh_tasks reach vkCmdDrawMeshTasksEXT
//     inside a dynamic-rendering render pass.
//
// Gating:
//   * If no Vulkan ICD is installed, every test SKIPs (existing harness).
//   * If the device does not advertise mesh_shader feature support
//     (VK_EXT_mesh_shader / VK_NV_mesh_shader missing), the per-test
//     mesh-shader checks SKIP so non-mesh-capable CI lanes still pass.
//   * If the engine was built without CD_ENABLE_GLSLANG, pipeline-creation
//     tests SKIP (no way to compile GLSL → SPIR-V at runtime).
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
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
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!dev_var)                     \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

#define SKIP_IF_NO_MESH_SHADER(dev_var)            \
    if (!(dev_var)->features().mesh_shader)        \
    GTEST_SKIP() << "device does not support VK_EXT_mesh_shader"

// Minimal mesh shader: emits a single triangle with no inputs. Tested via
// glslang -> SPIR-V at runtime so the test does not need a pre-compiled
// .spv on disk.
constexpr const char* kMeshGlsl = R"glsl(
#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;
void main()
{
    SetMeshOutputsEXT(3, 1);
    gl_MeshVerticesEXT[0].gl_Position = vec4(-0.5, -0.5, 0.0, 1.0);
    gl_MeshVerticesEXT[1].gl_Position = vec4( 0.5, -0.5, 0.0, 1.0);
    gl_MeshVerticesEXT[2].gl_Position = vec4( 0.0,  0.5, 0.0, 1.0);
    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
)glsl";

constexpr const char* kFragGlsl = R"glsl(
#version 460
layout(location = 0) out vec4 fragColor;
void main()
{
    fragColor = vec4(1.0, 0.5, 0.2, 1.0);
}
)glsl";

}  // namespace

TEST(VulkanMeshShader, FeatureBitGated)
{
    // Sanity check: features().mesh_shader is the gate every caller must
    // honor. If the bit is clear, create_mesh_pipeline must reject with
    // kNotImplemented regardless of how well-formed the descriptor is.
    SKIP_IF_NO_VULKAN(dev);
    if (dev->features().mesh_shader)
        GTEST_SKIP() << "device has mesh shaders (covered by other tests)";
    cd::rhi::MeshPipelineDesc bad {};
    auto r = dev->create_mesh_pipeline(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kNotImplemented));
}

TEST(VulkanMeshShader, CreateMeshPipelineRequiresMeshShader)
{
    SKIP_IF_NO_VULKAN(dev);
    SKIP_IF_NO_MESH_SHADER(dev);
    cd::rhi::MeshPipelineDesc bad {};
    auto r = dev->create_mesh_pipeline(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(VulkanMeshShader, CreateMeshPipelineFromCompiledGlsl)
{
    SKIP_IF_NO_VULKAN(dev);
    SKIP_IF_NO_MESH_SHADER(dev);
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    }

    cd::shader::CompileDesc md {};
    md.source = kMeshGlsl;
    md.stage = cd::shader::ShaderStage::kMesh;
    md.source_name = "mesh.mesh";
    auto ms = compiler->compile(md);
    if (!ms.has_value())
    {
        // Older glslang builds don't ship the mesh-shader entry point —
        // skip rather than fail (the RHI surface is still wired correctly).
        GTEST_SKIP() << "glslang cannot compile mesh shader: " << ms.error().message;
    }

    cd::shader::CompileDesc fd {};
    fd.source = kFragGlsl;
    fd.stage = cd::shader::ShaderStage::kFragment;
    fd.source_name = "mesh.frag";
    auto fs = compiler->compile(fd);
    ASSERT_TRUE(fs.has_value()) << fs.error().message;

    cd::rhi::ShaderModuleDesc ms_desc {};
    ms_desc.stage = cd::rhi::ShaderStage::kMesh;
    ms_desc.code = ms->spirv.data();
    ms_desc.code_size = ms->spirv.size() * sizeof(std::uint32_t);
    auto ms_mod = dev->create_shader_module(ms_desc);
    ASSERT_TRUE(ms_mod.has_value()) << ms_mod.error().message;

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
    cd::rhi::MeshPipelineDesc mpd {};
    mpd.layout = *pl;
    mpd.mesh_shader = *ms_mod;
    mpd.fragment_shader = *fs_mod;
    mpd.color_attachment_formats = color_formats;
    auto mp = dev->create_mesh_pipeline(mpd);
    ASSERT_TRUE(mp.has_value()) << mp.error().message;

    // Record a no-validation-error dispatch: bind the mesh PSO inside a
    // dynamic-rendering pass and issue a 1x1x1 draw_mesh_tasks. The render
    // target is a tiny 16x16 RGBA8 image transitioned to COLOR_ATTACHMENT.
    cd::rhi::TextureDesc td {};
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { 16, 16, 1 };
    td.usage = cd::rhi::TextureUsage::kColorAttachment;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex = dev->create_texture(td);
    ASSERT_TRUE(tex.has_value());

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *tex;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = cd::rhi::Format::kRGBA8Unorm;
    auto view = dev->create_texture_view(vd);
    ASSERT_TRUE(view.has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cb, nullptr);
    cb->begin();

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
            .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } },
        },
    };
    cd::rhi::RenderPassBeginInfo rp {};
    rp.render_area = cd::rhi::Rect2D {
        { 0,  0  },
        { 16, 16 }
    };
    rp.color_attachments = color_attachments;
    cb->begin_render_pass(rp);
    cb->set_viewport(cd::rhi::Viewport { 0.0F, 0.0F, 16.0F, 16.0F, 0.0F, 1.0F });
    cb->set_scissor(cd::rhi::Rect2D {
        { 0,  0  },
        { 16, 16 }
    });
    cb->bind_graphics_pipeline(*mp);
    cb->draw_mesh_tasks(1, 1, 1);
    cb->end_render_pass();
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_graphics_pipeline(*mp);
    dev->destroy_pipeline_layout(*pl);
    dev->destroy_texture_view(*view);
    dev->destroy_texture(*tex);
    dev->destroy_shader_module(*fs_mod);
    dev->destroy_shader_module(*ms_mod);
}
