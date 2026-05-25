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

    // ---- Cleanup ---------------------------------------------------------
    if (view_h.is_valid()) device.destroy_texture_view(view_h);
    if (tex_h.is_valid())  device.destroy_texture(tex_h);
    device.wait_idle();

    std::printf("[gl] summary: %d created, %d failed\n", created, failed);
    return failed == 0 ? 0 : 2;
}
