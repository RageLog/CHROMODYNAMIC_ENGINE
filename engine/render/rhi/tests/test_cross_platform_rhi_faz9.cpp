// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_cross_platform_rhi_faz9.cpp
// FAZ 9 Waves 9.1 & 9.2 — Metal Shader/Device Parity & WebGPU (Dawn/Emscripten)
// RHI Parity Tests.
// =============================================================================
#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/rhi/metal/MetalDevice.hpp>
#include <cd/rhi/metal/MetalShaderToolchain.hpp>
#include <cd/rhi/webgpu/WebGpuDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{

constexpr const char* kGluonFroxelAndFogCS = R"glsl(
#version 450
#extension GL_GOOGLE_include_directive : enable
#include <cd/gluon/froxel_cluster.glsl>
#include <cd/gluon/atmosphere_fog.glsl>

layout(local_size_x = 8, local_size_y = 4, local_size_z = 2) in;
layout(set = 0, binding = 0) buffer ScatteringOut {
    vec4 slice_rgba[];
} out_buf;

layout(push_constant) uniform FogPush {
    vec4 clip_planes;
} pc;

void main()
{
    uint tz = cd_cluster_z_slice(12.5, 0.1, 200.0, 24u);
    uint idx = cd_cluster_flat_index(uvec3(gl_GlobalInvocationID.xy, tz), uvec3(16u, 9u, 24u));
    float phase = cd_phase_cornette_shanks(0.75, 0.6);
    vec3 trans = cd_beer_lambert(vec3(0.02, 0.03, 0.05), 10.0);
    out_buf.slice_rgba[idx] = vec4(trans * phase, 1.0) + pc.clip_planes * 0.0;
}
)glsl";

TEST(Faz9Wave91MetalParity, GluonFroxelAndAtmosphereFogCompileToMslWithArgumentBuffersAndWorkgroup)
{
    auto compiler = cd::shader::make_glslang_compiler();
    if (!compiler)
    {
        GTEST_SKIP() << "glslang compiler unavailable";
    }

    cd::rhi::metal::GlslToMslDesc desc {};
    desc.glsl_source = kGluonFroxelAndFogCS;
    desc.stage = cd::rhi::ShaderStage::kCompute;
    desc.source_name = "gluon_froxel_fog.comp";
    desc.binding.argument_buffers = true;
    desc.binding.push_constant_buffer_index = cd::rhi::metal::kPushConstantBufferIndex;

    auto msl = cd::rhi::metal::compose_glsl_to_msl(*compiler, desc);
    ASSERT_TRUE(msl.has_value()) << msl.error().message;
    EXPECT_FALSE(msl->source.empty());
    EXPECT_FALSE(msl->entry_point.empty());
    EXPECT_EQ(msl->workgroup.x, 8U);
    EXPECT_EQ(msl->workgroup.y, 4U);
    EXPECT_EQ(msl->workgroup.z, 2U);
    EXPECT_NE(msl->source.find("[[buffer(0)]]"), std::string::npos);

#if !defined(__APPLE__)
    auto dev = cd::rhi::metal::create_metal_device();
    EXPECT_FALSE(dev.has_value());
    EXPECT_EQ(dev.error().code, static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kBackendInitFailed));
#endif
}

TEST(Faz9Wave92WebGpuParity, DawnAndEmscriptenWebGpuDeviceFullResourceAndBufferRoundTrip)
{
    cd::rhi::webgpu::WebGpuCreateInfo dawn_info {};
    dawn_info.runtime = cd::rhi::webgpu::WebGpuRuntimeKind::kDawnDesktop;
    auto dawn_res = cd::rhi::webgpu::create_webgpu_device(dawn_info);
    ASSERT_TRUE(dawn_res.has_value());
    auto& dev = **dawn_res;

    EXPECT_NE(dev.adapter_name().find("Dawn"), std::string_view::npos);
    EXPECT_TRUE(dev.features().timestamp_queries);
    EXPECT_TRUE(dev.features().sampler_anisotropy);
    EXPECT_NE(cd::rhi::webgpu::translate_to_wgsl_source("#version 450\nvoid main() {}", cd::rhi::ShaderStage::kCompute)
                  .find("@compute @workgroup_size"),
              std::string::npos);

    cd::rhi::BufferDesc bd {};
    bd.size = 64;
    bd.usage = cd::rhi::BufferUsage::kStorage | cd::rhi::BufferUsage::kTransferSrc | cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf = dev.create_buffer(bd);
    ASSERT_TRUE(buf.has_value());

    std::array<std::byte, 4> payload {
        std::byte { 0xCA }, std::byte { 0xFE }, std::byte { 0xBA }, std::byte { 0xBE }
    };
    ASSERT_TRUE(dev.upload_buffer(*buf, 0U, payload).has_value());

    std::array<std::byte, 4> readback {};
    ASSERT_TRUE(dev.download_buffer(*buf, 0U, readback).has_value());
    EXPECT_EQ(readback, payload);

    constexpr const char* kDummyGlsl = "#version 450\nvoid main() {}\n";
    cd::rhi::ShaderModuleDesc smd {};
    smd.stage = cd::rhi::ShaderStage::kVertex;
    smd.code = reinterpret_cast<const std::byte*>(kDummyGlsl);
    smd.code_size = std::char_traits<char>::length(kDummyGlsl);
    auto vs = dev.create_shader_module(smd);
    ASSERT_TRUE(vs.has_value());

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.vertex_shader = *vs;
    auto gp = dev.create_graphics_pipeline(gpd);
    ASSERT_TRUE(gp.has_value());

    dev.destroy_graphics_pipeline(*gp);
    dev.destroy_shader_module(*vs);
    dev.destroy_buffer(*buf);

    cd::rhi::webgpu::WebGpuCreateInfo em_info {};
    em_info.runtime = cd::rhi::webgpu::WebGpuRuntimeKind::kEmscriptenBrowser;
    auto em_res = cd::rhi::webgpu::create_webgpu_device(em_info);
    ASSERT_TRUE(em_res.has_value());
    EXPECT_NE((*em_res)->adapter_name().find("Emscripten"), std::string_view::npos);
}

}  // namespace
