// =============================================================================
// CHROMODYNAMIC — samples/hello_opengl_resources
//
// Phase 148-lite / v0.99.79 — exercises the partial OpenGL backend
// shipped across Phases 137 (buffer), 143 (texture + view), 144
// (sampler). Boots the GL device, creates one of each, prints what
// it managed, destroys, exits.
//
// The "full" hello_opengl sample (with swapchain + draw call) waits
// on Phases 145-148 (swapchain / shader / pipeline). This sample
// validates the resource-creation half of the backend works on the
// host driver — useful as a CI smoke test once the partial backend
// is the engine's documented minimum.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi_opengl/OpenGLDevice.hpp>

#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_opengl_resources\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_opengl::GLCreateInfo info {};
    info.app_name = "hello_opengl_resources";
    info.min_version = 30;
    info.enable_validation = false;
    auto dev_r = cd::rhi_opengl::create_gl_device(info);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[gl] create_gl_device failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;
    std::printf("[gl] adapter: %.*s\n",
                static_cast<int>(device.adapter_name().size()),
                device.adapter_name().data());

    int created = 0;
    int failed = 0;

    // ---- Buffer (Phase 137) ---------------------------------------------
    {
        cd::rhi::BufferDesc bd {};
        bd.size = 256;
        bd.usage = cd::rhi::BufferUsage::kStorage;
        bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
        bd.debug_name = "hello_opengl_resources_buf";
        auto r = device.create_buffer(bd);
        if (r.has_value())
        {
            std::printf("[gl] buffer OK (256 B, storage, gpu-only)\n");
            device.destroy_buffer(*r);
            ++created;
        }
        else
        {
            std::printf("[gl] buffer FAILED: %.*s\n",
                        static_cast<int>(r.error().message.size()),
                        r.error().message.data());
            ++failed;
        }
    }

    // ---- Texture + view (Phase 143) -------------------------------------
    cd::rhi::TextureHandle tex_h {};
    {
        cd::rhi::TextureDesc td {};
        td.type = cd::rhi::TextureType::k2D;
        td.format = cd::rhi::Format::kRGBA8Unorm;
        td.extent = { 64, 64, 1 };
        td.mip_levels = 1;
        td.usage = cd::rhi::TextureUsage::kSampled;
        td.debug_name = "hello_opengl_resources_tex";
        auto r = device.create_texture(td);
        if (r.has_value())
        {
            tex_h = *r;
            std::printf("[gl] texture OK (64x64 RGBA8)\n");
            ++created;
        }
        else
        {
            std::printf("[gl] texture FAILED: %.*s\n",
                        static_cast<int>(r.error().message.size()),
                        r.error().message.data());
            ++failed;
        }
    }
    cd::rhi::TextureViewHandle view_h {};
    if (tex_h.is_valid())
    {
        cd::rhi::TextureViewDesc vd {};
        vd.texture = tex_h;
        vd.format = cd::rhi::Format::kRGBA8Unorm;
        auto r = device.create_texture_view(vd);
        if (r.has_value())
        {
            view_h = *r;
            std::printf("[gl] texture view OK\n");
            ++created;
        }
        else
        {
            std::printf("[gl] texture view FAILED: %.*s\n",
                        static_cast<int>(r.error().message.size()),
                        r.error().message.data());
            ++failed;
        }
    }

    // ---- Sampler (Phase 144) --------------------------------------------
    {
        cd::rhi::SamplerDesc sd {};
        sd.min_filter = cd::rhi::SamplerFilter::kLinear;
        sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
        sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
        sd.address_u = cd::rhi::SamplerAddressMode::kRepeat;
        sd.address_v = cd::rhi::SamplerAddressMode::kRepeat;
        sd.address_w = cd::rhi::SamplerAddressMode::kClampToEdge;
        auto r = device.create_sampler(sd);
        if (r.has_value())
        {
            std::printf("[gl] sampler OK (linear+linear, repeat/repeat/clamp)\n");
            device.destroy_sampler(*r);
            ++created;
        }
        else
        {
            std::printf("[gl] sampler FAILED: %.*s\n",
                        static_cast<int>(r.error().message.size()),
                        r.error().message.data());
            ++failed;
        }
    }

    // ---- Vertex shader (Phase 146) --------------------------------------
    constexpr const char* kVs = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
void main() {
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
)glsl";
    constexpr const char* kFs = R"glsl(
#version 330 core
out vec4 frag;
void main() {
  frag = vec4(1.0, 0.5, 0.2, 1.0);
}
)glsl";
    cd::rhi::ShaderModuleHandle vs_h {};
    cd::rhi::ShaderModuleHandle fs_h {};
    {
        cd::rhi::ShaderModuleDesc smd {};
        smd.stage = cd::rhi::ShaderStage::kVertex;
        smd.code = kVs;
        smd.code_size = std::strlen(kVs);
        auto r = device.create_shader_module(smd);
        if (r.has_value()) { vs_h = *r; std::printf("[gl] VS shader OK\n"); ++created; }
        else { std::printf("[gl] VS shader FAILED: %.*s\n", static_cast<int>(r.error().message.size()), r.error().message.data()); ++failed; }
    }
    {
        cd::rhi::ShaderModuleDesc smd {};
        smd.stage = cd::rhi::ShaderStage::kFragment;
        smd.code = kFs;
        smd.code_size = std::strlen(kFs);
        auto r = device.create_shader_module(smd);
        if (r.has_value()) { fs_h = *r; std::printf("[gl] FS shader OK\n"); ++created; }
        else { std::printf("[gl] FS shader FAILED: %.*s\n", static_cast<int>(r.error().message.size()), r.error().message.data()); ++failed; }
    }

    // ---- Pipeline (Phase 147) -------------------------------------------
    cd::rhi::PipelineLayoutHandle pl_h {};
    cd::rhi::GraphicsPipelineHandle pipe_h {};
    if (vs_h.is_valid() && fs_h.is_valid())
    {
        cd::rhi::PipelineLayoutDesc pld {};
        auto plr = device.create_pipeline_layout(pld);
        if (plr.has_value()) { pl_h = *plr; std::printf("[gl] pipeline layout OK\n"); ++created; }
        cd::rhi::GraphicsPipelineDesc gpd {};
        gpd.layout = pl_h;
        gpd.vertex_shader = vs_h;
        gpd.fragment_shader = fs_h;
        auto gpr = device.create_graphics_pipeline(gpd);
        if (gpr.has_value()) { pipe_h = *gpr; std::printf("[gl] graphics pipeline OK (linked)\n"); ++created; }
        else { std::printf("[gl] graphics pipeline FAILED: %.*s\n", static_cast<int>(gpr.error().message.size()), gpr.error().message.data()); ++failed; }
    }

    // ---- Cleanup ---------------------------------------------------------
    if (pipe_h.is_valid()) device.destroy_graphics_pipeline(pipe_h);
    if (pl_h.is_valid())   device.destroy_pipeline_layout(pl_h);
    if (vs_h.is_valid())   device.destroy_shader_module(vs_h);
    if (fs_h.is_valid())   device.destroy_shader_module(fs_h);
    if (view_h.is_valid()) device.destroy_texture_view(view_h);
    if (tex_h.is_valid())  device.destroy_texture(tex_h);
    device.wait_idle();

    std::printf("[gl] summary: %d created, %d failed\n", created, failed);
    return failed == 0 ? 0 : 2;
}
