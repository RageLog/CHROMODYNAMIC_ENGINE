// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_asset_pipeline/main.cpp
//
// phase1150 — sample consolidation (SAMPLES_CONSOLIDATION_PLAN.md Batch-3):
// 6 asset samples folded into ONE binary.
//
//   Section  | Former binary          | What it proves
//   ---------+------------------------+------------------------------------------
//   mesh     | hello_mesh             | VB+IB upload, indexed draw, vertex layout
//   texture  | hello_texture          | staging→image, sampler, descriptor set
//   obj      | hello_obj              | cd::asset_obj parse + orbit camera
//   cooked   | hello_cooked           | cd::asset_cdmesh cook→load→VB/IB
//   gltf     | hello_gltf             | cd::asset_gltf load, textures, frustum cull
//   textured_cooked | hello_textured_cooked | BC7 compress→.cdtex→GPU
//
// Usage:
//   hello_asset_pipeline                        # runs all 6 sections in order
//   hello_asset_pipeline mesh                   # single section
//   hello_asset_pipeline obj <path/to/file.obj> # section + optional asset path
//   hello_asset_pipeline gltf <path/to/file.gltf>
//   hello_asset_pipeline cooked [<path.cdmesh>]
//   hello_asset_pipeline textured_cooked [<path.cdtex>]
//   hello_asset_pipeline --headless 3           # all sections, 3 frames each
//
// Exit codes:
//   0  — all selected sections succeeded
//   Non-zero — first failing section's exit code (run stops at first failure)
// =============================================================================
#include "SampleRuntime.hpp"

// asset loaders
#include <cd/asset/cdmesh/CdMesh.hpp>
#include <cd/asset/cdtex/CdTex.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/image/Bc7.hpp>
#include <cd/asset/obj/ObjLoader.hpp>

// engine
#include <cd/camera/Camera.hpp>
#include <cd/camera/Frustum.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// =============================================================================
// Shared helpers (deduplicated from the 6 originals)
// =============================================================================
namespace
{

// ---------------------------------------------------------------------------
// GPU resource helpers
// ---------------------------------------------------------------------------

[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice& dev, std::span<const std::byte> bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = bytes.size();
    bd.usage  = usage;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    if (auto u = dev.upload_buffer(*r, 0, bytes); !u.has_value())
    {
        dev.destroy_buffer(*r);
        return {};
    }
    return *r;
}

struct DepthTarget
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        image = {};
        view  = {};
    }
};

[[nodiscard]] bool
create_depth_target(cd::rhi::IDevice& dev, cd::rhi::Extent2D size,
                    cd::rhi::Format fmt, DepthTarget& out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = fmt;
    td.extent       = { size.width, size.height, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kDepthStencilAttachment;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return false;

    cd::rhi::TextureViewDesc vd {};
    vd.texture     = *img;
    vd.type        = cd::rhi::TextureType::k2D;
    vd.format      = fmt;
    vd.base_mip    = 0; vd.mip_count   = 1;
    vd.base_layer  = 0; vd.layer_count = 1;
    auto view = dev.create_texture_view(vd);
    if (!view.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image = *img;
    out.view  = *view;
    return true;
}

struct GpuTexture
{
    cd::rhi::TextureHandle     image {};
    cd::rhi::TextureViewHandle view  {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())  dev.destroy_texture_view(view);
        if (image.is_valid()) dev.destroy_texture(image);
        image = {};
        view  = {};
    }
};

[[nodiscard]] bool upload_texture_2d(
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle image,
    cd::rhi::BufferHandle staging,
    std::uint32_t w, std::uint32_t h)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) return false;
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 1> to_dst {
        cd::rhi::TextureBarrier {
            .texture = image,
            .from    = cd::rhi::ResourceState::kUndefined,
            .to      = cd::rhi::ResourceState::kTransferDst,
            .range   = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        }
    };
    cmd->barrier({}, to_dst);

    std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
            .buffer_offset = 0, .mip_level = 0,
            .base_layer = 0, .layer_count = 1,
            .image_offset = { 0, 0, 0 },
            .image_extent = { w, h, 1 },
        }
    };
    cmd->copy_buffer_to_image(staging, image, regions);

    std::array<cd::rhi::TextureBarrier, 1> to_read {
        cd::rhi::TextureBarrier {
            .texture = image,
            .from    = cd::rhi::ResourceState::kTransferDst,
            .to      = cd::rhi::ResourceState::kShaderResource,
            .range   = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
        }
    };
    cmd->barrier({}, to_read);

    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
    return true;
}

[[nodiscard]] bool create_and_upload_texture(
    cd::rhi::IDevice& dev,
    std::span<const std::uint8_t> rgba,
    std::uint32_t w, std::uint32_t h,
    GpuTexture& out)
{
    out.destroy(dev);

    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k2D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { w, h, 1 };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex = dev.create_texture(td);
    if (!tex.has_value()) return false;
    out.image = *tex;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture     = out.image;
    tvd.type        = cd::rhi::TextureType::k2D;
    tvd.format      = cd::rhi::Format::kRGBA8Unorm;
    tvd.base_mip    = 0; tvd.mip_count   = 1;
    tvd.base_layer  = 0; tvd.layer_count = 1;
    auto view = dev.create_texture_view(tvd);
    if (!view.has_value()) { out.destroy(dev); return false; }
    out.view = *view;

    const std::span<const std::byte> px_bytes {
        reinterpret_cast<const std::byte*>(rgba.data()), rgba.size()
    };
    const auto staging = make_upload_buffer(dev, px_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid()) { out.destroy(dev); return false; }

    const bool ok = upload_texture_2d(dev, out.image, staging, w, h);
    dev.destroy_buffer(staging);
    if (!ok) { out.destroy(dev); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Window / device / renderer bootstrap (shared pattern across sections)
// ---------------------------------------------------------------------------

struct DeviceContext
{
    std::unique_ptr<cd::platform::IWindow> window;
    std::unique_ptr<cd::rhi::IDevice>      device;
    std::optional<cd::render::Renderer>    renderer;
};

[[nodiscard]] bool
boot_vulkan(const char* title, std::uint32_t w, std::uint32_t h, DeviceContext& ctx)
{
    cd::platform::WindowDesc wd {};
    wd.title  = title;
    wd.width  = w;
    wd.height = h;
    auto wr = cd::platform::create_window(wd);
    if (!wr.has_value())
    {
        std::fprintf(stderr, "window: %.*s\n",
            static_cast<int>(wr.error().message.size()), wr.error().message.data());
        return false;
    }
    ctx.window = std::move(*wr);

    cd::rhi::vulkan::VulkanCreateInfo vci {};
    auto dr = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dr.has_value())
    {
        std::fprintf(stderr, "device: %.*s\n",
            static_cast<int>(dr.error().message.size()), dr.error().message.data());
        return false;
    }
    ctx.device = std::move(*dr);

    cd::render::RendererDesc rd {};
    rd.device                  = ctx.device.get();
    rd.swapchain.window_handle  = ctx.window->native_window_handle();
    rd.swapchain.display_handle = ctx.window->native_display_handle();
    rd.swapchain.extent         = { ctx.window->width(), ctx.window->height() };
    rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight         = 2;
    auto rr = cd::render::Renderer::create(rd);
    if (!rr.has_value())
    {
        std::fprintf(stderr, "renderer: %.*s\n",
            static_cast<int>(rr.error().message.size()), rr.error().message.data());
        return false;
    }
    ctx.renderer.emplace(std::move(*rr));
    return true;
}

}  // namespace

// =============================================================================
// Section A — mesh (ex hello_mesh)
// Draws an RGBA quad from explicit vertex+index buffers.
// =============================================================================
namespace
{

[[nodiscard]] int run_mesh(const cd::sample::Runtime& runtime)
{
    std::printf("=== [mesh] indexed quad from vertex+index buffers ===\n");
    std::fflush(stdout);

    struct Vtx { float pos[2]; float color[3]; };

    constexpr std::array<Vtx, 4> kVerts {{
        { { -0.6F, -0.6F }, { 1.0F, 0.0F, 0.0F } },
        { {  0.6F, -0.6F }, { 0.0F, 1.0F, 0.0F } },
        { {  0.6F,  0.6F }, { 0.0F, 0.0F, 1.0F } },
        { { -0.6F,  0.6F }, { 1.0F, 1.0F, 0.0F } },
    }};
    constexpr std::array<std::uint16_t, 6> kIdx { 0, 1, 2, 0, 2, 3 };

    constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() { gl_Position = vec4(in_pos, 0.0, 1.0); v_color = in_color; }
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [mesh]", 800, 600, ctx))
        return 1;
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(kVerts.data()), kVerts.size() * sizeof(Vtx)
    };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(kIdx.data()), kIdx.size() * sizeof(std::uint16_t)
    };
    auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
    {
        std::fprintf(stderr, "[mesh] buffer upload failed\n");
        return 2;
    }

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { std::fprintf(stderr, "[mesh] no glslang\n"); return 3; }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { .binding = 0, .stride = sizeof(Vtx), .per_instance = false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { .location = 0, .binding = 0, .format = cd::rhi::Format::kRG32Float,  .offset = offsetof(Vtx, pos)   },
        cd::rhi::VertexAttribute { .location = 1, .binding = 0, .format = cd::rhi::Format::kRGB32Float, .offset = offsetof(Vtx, color) },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };

    cd::material::MaterialDesc md {};
    md.vertex_glsl            = kVS;
    md.fragment_glsl          = kFS;
    md.vertex_bindings        = kBindings;
    md.vertex_attributes      = kAttrs;
    md.color_attachment_formats = kColorFmts;
    md.topology               = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull            = cd::rhi::CullMode::kNone;
    md.name                   = "mesh_quad";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
    {
        std::fprintf(stderr, "[mesh] material: %.*s\n",
            static_cast<int>(mat_r.error().message.size()), mat_r.error().message.data());
        return 4;
    }
    auto& material = *mat_r;

    std::printf("[mesh] ready. auto-close after headless frame limit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(32);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0) return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) return false;
        needs_rebuild = false;
        return true;
    };

    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild()) continue;

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 5;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.08F, 0.10F, 0.14F, 1.0F } },
            }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area     = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        material.apply(cmd);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(static_cast<std::uint32_t>(kIdx.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 6;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    std::printf("[mesh] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// Section B — texture (ex hello_texture)
// Procedural checker → staging buffer → device-local image → sampler → quad.
// =============================================================================
namespace
{

[[nodiscard]] int run_texture(const cd::sample::Runtime& runtime)
{
    std::printf("=== [texture] checker pattern via descriptor set ===\n");
    std::fflush(stdout);

    constexpr std::uint32_t kTexSz = 256;

    auto make_checker = []
    {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(kTexSz) * kTexSz * 4);
        for (std::uint32_t y = 0; y < kTexSz; ++y)
            for (std::uint32_t x = 0; x < kTexSz; ++x)
            {
                const bool dark = ((x >> 5) ^ (y >> 5)) & 1u;
                const auto u    = static_cast<float>(x) / static_cast<float>(kTexSz - 1);
                const auto v    = static_cast<float>(y) / static_cast<float>(kTexSz - 1);
                const auto base = dark ? 60U : 200U;
                const auto i    = (y * kTexSz + x) * 4U;
                px[i + 0] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(u * 40.0F));
                px[i + 1] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(v * 40.0F));
                px[i + 2] = static_cast<std::uint8_t>(base);
                px[i + 3] = 255U;
            }
        return px;
    };

    struct Vtx { float pos[2]; float uv[2]; };
    constexpr std::array<Vtx, 4> kVerts {{
        { { -0.7F, -0.7F }, { 0.0F, 0.0F } },
        { {  0.7F, -0.7F }, { 1.0F, 0.0F } },
        { {  0.7F,  0.7F }, { 1.0F, 1.0F } },
        { { -0.7F,  0.7F }, { 0.0F, 1.0F } },
    }};
    constexpr std::array<std::uint16_t, 6> kIdx { 0, 1, 2, 0, 2, 3 };

    constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vec4(in_pos, 0.0, 1.0); v_uv = in_uv; }
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = texture(u_tex, v_uv); }
)glsl";

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [texture]", 800, 800, ctx))
        return 1;
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    // Create device-local image + view
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D; td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { kTexSz, kTexSz, 1 }; td.mip_levels = 1; td.array_layers = 1;
    td.usage  = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_r = device.create_texture(td);
    if (!tex_r.has_value()) { std::fprintf(stderr, "[texture] create tex\n"); return 2; }
    const auto texture = *tex_r;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = texture; tvd.type = cd::rhi::TextureType::k2D;
    tvd.format = cd::rhi::Format::kRGBA8Unorm;
    tvd.base_mip = 0; tvd.mip_count = 1; tvd.base_layer = 0; tvd.layer_count = 1;
    auto view_r = device.create_texture_view(tvd);
    if (!view_r.has_value())
    {
        device.destroy_texture(texture);
        std::fprintf(stderr, "[texture] create view\n"); return 3;
    }
    const auto tex_view = *view_r;

    // Sampler
    cd::rhi::SamplerDesc sd {};
    sd.mag_filter = cd::rhi::SamplerFilter::kLinear; sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sd.address_u = sd.address_v = sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_r = device.create_sampler(sd);
    if (!samp_r.has_value())
    {
        device.destroy_texture_view(tex_view); device.destroy_texture(texture);
        std::fprintf(stderr, "[texture] sampler\n"); return 4;
    }
    const auto sampler = *samp_r;

    // Upload checker
    const auto pixels = make_checker();
    const std::span<const std::byte> px_bytes {
        reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()
    };
    const auto staging = make_upload_buffer(device, px_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid()) { std::fprintf(stderr, "[texture] staging\n"); return 5; }
    if (!upload_texture_2d(device, texture, staging, kTexSz, kTexSz))
    { std::fprintf(stderr, "[texture] upload\n"); return 6; }
    device.destroy_buffer(staging);

    // Geometry buffers
    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(kVerts.data()), kVerts.size() * sizeof(Vtx)
    };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(kIdx.data()), kIdx.size() * sizeof(std::uint16_t)
    };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid()) { std::fprintf(stderr, "[texture] geom buffers\n"); return 7; }

    // Material + descriptor
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { std::fprintf(stderr, "[texture] no glslang\n"); return 8; }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vtx), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRG32Float, offsetof(Vtx, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRG32Float, offsetof(Vtx, uv)  },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS; md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings; md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFmts;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "tex_quad";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value()) { std::fprintf(stderr, "[texture] material\n"); return 9; }
    auto& material = *mat_r;

    auto inst_r = cd::material::MaterialInstance::create(device, material);
    if (!inst_r.has_value()) { std::fprintf(stderr, "[texture] instance\n"); return 10; }
    auto& instance = *inst_r;

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite {
            .binding = 0, .array_element = 0,
            .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .buffer = {}, .buffer_offset = 0, .buffer_range = 0,
            .view = tex_view, .sampler = sampler }
    };
    if (!instance.update(writes).has_value()) { std::fprintf(stderr, "[texture] desc update\n"); return 11; }

    std::printf("[texture] ready.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(32);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0) return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) return false;
        needs_rebuild = false;
        return true;
    };

    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild()) continue;

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 12;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } },
            }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        material.apply(cmd);
        instance.bind(cmd, 0);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(static_cast<std::uint32_t>(kIdx.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 13;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib); device.destroy_buffer(vb);
    device.destroy_sampler(sampler);
    device.destroy_texture_view(tex_view);
    device.destroy_texture(texture);
    std::printf("[texture] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// Section C — obj (ex hello_obj)
// Wavefront .obj viewer; falls back to inline cube when no path given.
// =============================================================================
namespace
{

[[nodiscard]] int run_obj(const cd::sample::Runtime& runtime, const char* obj_path)
{
    std::printf("=== [obj] Wavefront OBJ viewer ===\n");
    std::fflush(stdout);

    constexpr std::string_view kInlineCube = R"obj(
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
f 1 2 3 4
f 5 6 7 8
f 1 5 6 2
f 2 6 7 3
f 3 7 8 4
f 4 8 5 1
)obj";

    struct PushBlock { cd::math::Mat4f mvp {}; std::array<float, 4> base_color {}; };

    constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 base_color; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0); clip.y = -clip.y;
  gl_Position = clip; v_normal = in_normal; v_color = pc.base_color;
}
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_normal;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 n = normalize(v_normal); vec3 l = normalize(vec3(0.6, 0.8, 0.3));
  float ndotl = max(dot(n, l), 0.0);
  out_color = vec4(v_color.rgb * (0.25 + 0.75 * ndotl), v_color.a);
}
)glsl";

    cd::core::Result<cd::asset::obj::ObjMesh> loaded =
        std::unexpected(cd::asset::obj::obj_errors::make(cd::asset::obj::obj_errors::Code::kOk));

    if (obj_path != nullptr)
    {
        loaded = cd::asset::obj::load_obj(obj_path);
        if (!loaded.has_value())
        {
            std::fprintf(stderr, "[obj] load: %.*s\n",
                static_cast<int>(loaded.error().message.size()), loaded.error().message.data());
            return 1;
        }
        std::printf("[obj] loaded %s — %zu verts, %zu idx\n",
            obj_path, loaded->vertices.size(), loaded->indices.size());
    }
    else
    {
        loaded = cd::asset::obj::parse_obj(kInlineCube);
        if (!loaded.has_value()) { std::fprintf(stderr, "[obj] inline parse failed\n"); return 1; }
        std::printf("[obj] no path — using built-in cube.\n");
    }
    const auto& mesh = *loaded;
    std::fflush(stdout);

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [obj]", 1280, 720, ctx))
        return 2;
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    constexpr auto kDepthFmt = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth))
        return 3;
    bool depth_init = false;

    struct ObjVtx { float pos[3]; float normal[3]; float uv[2]; };
    static_assert(sizeof(ObjVtx) == sizeof(cd::asset::obj::ObjVertex));

    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(mesh.vertices.data()),
        mesh.vertices.size() * sizeof(cd::asset::obj::ObjVertex)
    };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(mesh.indices.data()),
        mesh.indices.size() * sizeof(std::uint32_t)
    };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid()) { std::fprintf(stderr, "[obj] buffers\n"); return 4; }

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { std::fprintf(stderr, "[obj] no glslang\n"); return 5; }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(ObjVtx), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(ObjVtx, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(ObjVtx, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(ObjVtx, uv)     },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            .offset = 0, .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS; md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings; md.vertex_attributes = kAttrs;
    md.color_attachment_formats = kColorFmts;
    md.depth_attachment_format  = kDepthFmt;
    md.push_constants = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true; md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "obj_lit";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value()) { std::fprintf(stderr, "[obj] material\n"); return 6; }
    auto& material = *mat_r;

    cd::camera::Camera cam = cd::camera::auto_frame_aabb(mesh.bbox_min, mesh.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.6F;

    std::printf("[obj] ready.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(32);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0) return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth)) return false;
        depth_init = false; needs_rebuild = false;
        return true;
    };

    auto t_prev = std::chrono::steady_clock::now();
    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild()) continue;

        const auto t_now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        orbit.update_auto(cam, dt);

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 7;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        if (!depth_init)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from = cd::rhi::ResourceState::kUndefined, .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                }
            };
            cmd.barrier({}, dbar);
            depth_init = true;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } } }
        };
        cd::rhi::DepthStencilAttachmentInfo da {};
        da.view = depth.view; da.depth_load = cd::rhi::LoadOp::kClear;
        da.depth_store = cd::rhi::StoreOp::kStore; da.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca; rp.depth_stencil = &da;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const auto vp = cd::camera::view_projection(cam, aspect);

        material.apply(cmd);
        PushBlock pb {}; pb.mvp = vp; pb.base_color = { 0.85F, 0.7F, 0.4F, 1.0F };
        cmd.push_constants(material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0, static_cast<std::uint32_t>(sizeof(pb)), &pb);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt32);
        cmd.draw_indexed(static_cast<std::uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 8;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib); device.destroy_buffer(vb);
    depth.destroy(device);
    std::printf("[obj] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// Section D — cooked (ex hello_cooked)
// Cook inline cube → .cdmesh (or load provided path) → GPU VB/IB → orbit.
// =============================================================================
namespace
{

[[nodiscard]] std::string make_self_cooked_cube()
{
    constexpr std::string_view kObj = R"obj(
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
f 1 2 3 4
f 5 6 7 8
f 1 5 6 2
f 2 6 7 3
f 3 7 8 4
f 4 8 5 1
)obj";
    auto obj = cd::asset::obj::parse_obj(kObj);
    if (!obj.has_value()) return {};

    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path  = std::filesystem::temp_directory_path() /
                       ("cd_cooked_cube_" + std::to_string(static_cast<std::uint64_t>(stamp)) +
                        "_" + std::to_string(seq.fetch_add(1)) + ".cdmesh");

    cd::asset::cdmesh::SaveDesc d {};
    d.vertices     = { reinterpret_cast<const std::uint8_t*>(obj->vertices.data()),
                       obj->vertices.size() * sizeof(cd::asset::obj::ObjVertex) };
    d.indices      = { reinterpret_cast<const std::uint8_t*>(obj->indices.data()),
                       obj->indices.size() * sizeof(std::uint32_t) };
    d.vertex_count = static_cast<std::uint32_t>(obj->vertices.size());
    d.index_count  = static_cast<std::uint32_t>(obj->indices.size());
    d.vertex_stride = sizeof(cd::asset::obj::ObjVertex);
    d.index_stride  = 4;
    d.bbox_min = obj->bbox_min;
    d.bbox_max = obj->bbox_max;
    if (!cd::asset::cdmesh::save(path.string(), d).has_value()) return {};
    return path.string();
}

[[nodiscard]] int run_cooked(const cd::sample::Runtime& runtime, const char* cdmesh_path)
{
    std::printf("=== [cooked] cdmesh cook→load→GPU roundtrip ===\n");
    std::fflush(stdout);

    struct PushBlock { cd::math::Mat4f mvp {}; std::array<float, 4> base_color {}; };

    constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; } pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_normal;
void main() {
  vec4 clip = pc.mvp * vec4(in_pos, 1.0); clip.y = -clip.y;
  gl_Position = clip; v_normal = in_normal;
}
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 tint; } pc;
layout(location = 0) in  vec3 v_normal;
layout(location = 0) out vec4 out_color;
void main() {
  vec3 n = normalize(v_normal); vec3 l = normalize(vec3(0.6, 0.8, 0.3));
  float ndotl = max(dot(n, l), 0.0);
  out_color = vec4(pc.tint.rgb * (0.25 + 0.75 * ndotl), 1.0);
}
)glsl";

    // Resolve path
    std::string path;
    bool self_cooked = false;
    if (cdmesh_path != nullptr)
    {
        path = cdmesh_path;
    }
    else
    {
        path = make_self_cooked_cube();
        if (path.empty())
        {
            std::fprintf(stderr, "[cooked] self-cook failed\n");
            return 1;
        }
        self_cooked = true;
        std::printf("[cooked] self-cooked %s\n", path.c_str());
    }

    auto cooked = cd::asset::cdmesh::load(path);
    if (!cooked.has_value())
    {
        std::fprintf(stderr, "[cooked] load: %.*s\n",
            static_cast<int>(cooked.error().message.size()), cooked.error().message.data());
        if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
        return 2;
    }
    const auto& mesh = *cooked;
    std::printf("[cooked] loaded %s (verts=%u stride=%u idx=%u stride=%u)\n",
        path.c_str(), mesh.vertex_count, mesh.vertex_stride, mesh.index_count, mesh.index_stride);
    std::fflush(stdout);

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [cooked]", 1024, 768, ctx))
    {
        if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
        return 3;
    }
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    constexpr auto kDepthFmt = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth))
        return 4;
    bool depth_init = false;

    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(mesh.vertex_blob.data()), mesh.vertex_blob.size()
    };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(mesh.index_blob.data()), mesh.index_blob.size()
    };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid()) { std::fprintf(stderr, "[cooked] buffers\n"); return 5; }

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { std::fprintf(stderr, "[cooked] no glslang\n"); return 6; }

    struct CookedVtx { float pos[3]; float normal[3]; float uv[2]; };
    static_assert(sizeof(CookedVtx) == 32);

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(CookedVtx), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(CookedVtx, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(CookedVtx, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(CookedVtx, uv)     },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            .offset = 0, .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS; md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings; md.vertex_attributes = kAttrs;
    md.color_attachment_formats = kColorFmts;
    md.depth_attachment_format  = kDepthFmt;
    md.push_constants = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true; md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "cooked";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value()) { std::fprintf(stderr, "[cooked] material\n"); return 7; }
    auto& material = *mat_r;

    cd::camera::Camera cam = cd::camera::auto_frame_aabb(mesh.bbox_min, mesh.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.6F;

    std::printf("[cooked] ready.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(32);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0) return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth)) return false;
        depth_init = false; needs_rebuild = false;
        return true;
    };

    auto t_prev = std::chrono::steady_clock::now();
    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild()) continue;

        const auto t_now = std::chrono::steady_clock::now();
        orbit.update_auto(cam, std::chrono::duration<float>(t_now - t_prev).count());
        t_prev = t_now;

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 8;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        if (!depth_init)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from = cd::rhi::ResourceState::kUndefined, .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                }
            };
            cmd.barrier({}, dbar);
            depth_init = true;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } } }
        };
        cd::rhi::DepthStencilAttachmentInfo da {};
        da.view = depth.view; da.depth_load = cd::rhi::LoadOp::kClear;
        da.depth_store = cd::rhi::StoreOp::kStore; da.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca; rp.depth_stencil = &da;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        material.apply(cmd);
        PushBlock pb {}; pb.mvp = cd::camera::view_projection(cam, aspect);
        pb.base_color = { 0.35F, 0.85F, 0.55F, 1.0F };
        cmd.push_constants(material.pipeline_layout(),
            cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            0, static_cast<std::uint32_t>(sizeof(pb)), &pb);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt32);
        cmd.draw_indexed(mesh.index_count, 1, 0, 0, 0);
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 9;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib); device.destroy_buffer(vb);
    depth.destroy(device);
    if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
    std::printf("[cooked] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// Section E — gltf (ex hello_gltf)
// Full Cook-Torrance textured glTF viewer with frustum culling.
// =============================================================================
namespace
{

[[nodiscard]] cd::asset::gltf::GltfScene make_gltf_fallback_scene()
{
    cd::asset::gltf::GltfScene scene;
    cd::asset::gltf::GltfMesh mesh;
    mesh.name = "fallback_quad";
    cd::asset::gltf::GltfPrimitive prim;
    prim.vertices = {
        cd::asset::gltf::GltfVertex { .position = { -0.5F, -0.5F, 0.0F }, .normal = { 0.F, 0.F, 1.F }, .texcoord0 = { 0.F, 0.F } },
        cd::asset::gltf::GltfVertex { .position = {  0.5F, -0.5F, 0.0F }, .normal = { 0.F, 0.F, 1.F }, .texcoord0 = { 1.F, 0.F } },
        cd::asset::gltf::GltfVertex { .position = {  0.5F,  0.5F, 0.0F }, .normal = { 0.F, 0.F, 1.F }, .texcoord0 = { 1.F, 1.F } },
        cd::asset::gltf::GltfVertex { .position = { -0.5F,  0.5F, 0.0F }, .normal = { 0.F, 0.F, 1.F }, .texcoord0 = { 0.F, 1.F } },
    };
    prim.indices = { 0, 1, 2, 0, 2, 3 };
    prim.material_index = -1;
    mesh.primitives.push_back(std::move(prim));
    scene.meshes.push_back(std::move(mesh));
    scene.instances.push_back(
        cd::asset::gltf::GltfInstance { 0, -1, cd::math::Mat4f::identity() }
    );
    scene.bbox_min = { -0.5F, -0.5F, 0.0F };
    scene.bbox_max = {  0.5F,  0.5F, 0.0F };
    return scene;
}

[[nodiscard]] int run_gltf(const cd::sample::Runtime& runtime, const char* gltf_path)
{
    std::printf("=== [gltf] glTF 2.0 textured viewer ===\n");
    std::fflush(stdout);

    struct GltfVtx { float pos[3]; float normal[3]; float uv[2]; };
    static_assert(sizeof(GltfVtx) == sizeof(cd::asset::gltf::GltfVertex));

    struct GltfPushBlock {
        cd::math::Mat4f mvp {};
        std::array<float, 4> base_color {};
        std::array<float, 4> mr_pad {};
        std::array<float, 4> camera_pos {};
    };
    static_assert(sizeof(GltfPushBlock) == 112);

    constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp; vec4 base_color; vec4 mr_pad; vec4 camera_pos;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
void main() {
  v_world_pos = in_pos; v_normal = in_normal; v_uv = in_uv;
  vec4 clip = pc.mvp * vec4(in_pos, 1.0); clip.y = -clip.y; gl_Position = clip;
}
)glsl";

    constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_base_color;
layout(push_constant) uniform PC {
  mat4 mvp; vec4 base_color; vec4 mr_pad; vec4 camera_pos;
} pc;
layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_normal;
layout(location = 2) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
const float PI = 3.14159265358979;
float D_GGX(float NoH, float a) { float a2=a*a; float d=(NoH*NoH)*(a2-1.0)+1.0; return a2/(PI*d*d+1e-7); }
float G_SchlickGGX(float NoV, float k) { return NoV/(NoV*(1.0-k)+k+1e-7); }
float G_Smith(float NoV, float NoL, float r) { float k=(r+1.0)*(r+1.0)/8.0; return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k); }
vec3 F_Schlick(float HoV, vec3 F0) { return F0+(vec3(1.0)-F0)*pow(clamp(1.0-HoV,0.0,1.0),5.0); }
void main() {
  vec4 tex = texture(u_base_color, v_uv);
  vec3 albedo = tex.rgb * pc.base_color.rgb;
  float metallic = clamp(pc.mr_pad.x,0.0,1.0);
  float roughness = clamp(pc.mr_pad.y,0.04,1.0);
  float ambient = pc.mr_pad.z;
  vec3 N = normalize(v_normal); vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  vec3 L = normalize(vec3(0.6,0.8,0.3)); vec3 H = normalize(L+V);
  float NoL=max(dot(N,L),0.0); float NoV=max(dot(N,V),0.0);
  float NoH=max(dot(N,H),0.0); float HoV=max(dot(H,V),0.0);
  vec3 F0 = mix(vec3(0.04),albedo,metallic);
  float D=D_GGX(NoH,roughness*roughness); float G=G_Smith(NoV,NoL,roughness);
  vec3 F=F_Schlick(HoV,F0);
  vec3 specular=(D*G*F)/max(4.0*NoV*NoL,1e-4);
  vec3 kD=(vec3(1.0)-F)*(1.0-metallic);
  vec3 direct=(kD*albedo/PI+specular)*NoL;
  out_color = vec4(direct + albedo*ambient, tex.a*pc.base_color.a);
}
)glsl";

    cd::asset::gltf::GltfScene scene;
    if (gltf_path != nullptr)
    {
        auto loaded = cd::asset::gltf::load_gltf(gltf_path);
        if (!loaded.has_value())
        {
            std::fprintf(stderr, "[gltf] %.*s\n",
                static_cast<int>(loaded.error().message.size()), loaded.error().message.data());
            return 1;
        }
        scene = std::move(*loaded);
        std::printf("[gltf] loaded %s meshes=%zu materials=%zu textures=%zu\n",
            gltf_path, scene.meshes.size(), scene.materials.size(), scene.textures.size());
    }
    else
    {
        scene = make_gltf_fallback_scene();
        std::printf("[gltf] no path — fallback quad. usage: hello_asset_pipeline gltf <file>\n");
    }
    std::fflush(stdout);

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [gltf]", 1280, 720, ctx))
        return 2;
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    constexpr auto kDepthFmt = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth))
        return 3;
    bool depth_init = false;

    // White 1x1 fallback texture
    constexpr std::array<std::uint8_t, 4> kWhite { 0xFF, 0xFF, 0xFF, 0xFF };
    GpuTexture white_tex {};
    if (!create_and_upload_texture(device, kWhite, 1, 1, white_tex))
    { std::fprintf(stderr, "[gltf] white tex\n"); return 4; }

    // Upload glTF textures
    std::vector<GpuTexture> gpu_textures(scene.textures.size());
    for (std::size_t i = 0; i < scene.textures.size(); ++i)
    {
        const auto& src = scene.textures[i];
        if (src.rgba.empty() || src.width == 0 || src.height == 0) continue;
        if (!create_and_upload_texture(device, src.rgba, src.width, src.height, gpu_textures[i]))
            std::fprintf(stderr, "[gltf] warn: texture %zu upload failed\n", i);
    }

    // Shared sampler
    cd::rhi::SamplerDesc sdesc {};
    sdesc.mag_filter = cd::rhi::SamplerFilter::kLinear; sdesc.min_filter = cd::rhi::SamplerFilter::kLinear;
    sdesc.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sdesc.address_u = sdesc.address_v = sdesc.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_r = device.create_sampler(sdesc);
    if (!samp_r.has_value()) { std::fprintf(stderr, "[gltf] sampler\n"); return 5; }
    const auto sampler = *samp_r;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { std::fprintf(stderr, "[gltf] no glslang\n"); return 6; }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(GltfVtx), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(GltfVtx, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(GltfVtx, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(GltfVtx, uv)     },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            .offset = 0, .size = static_cast<std::uint32_t>(sizeof(GltfPushBlock)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS; md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings; md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFmts;
    md.depth_attachment_format  = kDepthFmt;
    md.push_constants = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true; md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "gltf_textured";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value()) { std::fprintf(stderr, "[gltf] material\n"); return 7; }
    auto& material = *mat_r;

    // Build drawables
    struct Drawable
    {
        cd::rhi::BufferHandle vb {}; cd::rhi::BufferHandle ib {};
        std::uint32_t index_count { 0 }; int mesh_index { -1 };
        cd::math::Vec3f local_bbox_min { 0.F, 0.F, 0.F };
        cd::math::Vec3f local_bbox_max { 0.F, 0.F, 0.F };
        std::array<float, 4> base_color { 1.F, 1.F, 1.F, 1.F };
        float metallic { 0.F }; float roughness { 1.F };
        cd::material::MaterialInstance instance {};

        void destroy(cd::rhi::IDevice& d)
        {
            instance = {};
            if (ib.is_valid()) d.destroy_buffer(ib);
            if (vb.is_valid()) d.destroy_buffer(vb);
            ib = {}; vb = {};
        }
    };

    std::vector<Drawable> drawables;
    drawables.reserve(16);
    for (std::size_t m = 0; m < scene.meshes.size(); ++m)
    {
        for (const auto& prim : scene.meshes[m].primitives)
        {
            if (prim.vertices.empty() || prim.indices.empty()) continue;
            Drawable d {};
            d.mesh_index = static_cast<int>(m);

            const std::span<const std::byte> vb_bytes {
                reinterpret_cast<const std::byte*>(prim.vertices.data()),
                prim.vertices.size() * sizeof(cd::asset::gltf::GltfVertex)
            };
            const std::span<const std::byte> ib_bytes {
                reinterpret_cast<const std::byte*>(prim.indices.data()),
                prim.indices.size() * sizeof(std::uint32_t)
            };
            d.vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
            d.ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
            d.index_count = static_cast<std::uint32_t>(prim.indices.size());

            constexpr float kInf = std::numeric_limits<float>::infinity();
            cd::math::Vec3f mn { kInf, kInf, kInf };
            cd::math::Vec3f mx { -kInf, -kInf, -kInf };
            for (const auto& v : prim.vertices)
            {
                mn[0] = std::min(mn[0], v.position[0]); mn[1] = std::min(mn[1], v.position[1]); mn[2] = std::min(mn[2], v.position[2]);
                mx[0] = std::max(mx[0], v.position[0]); mx[1] = std::max(mx[1], v.position[1]); mx[2] = std::max(mx[2], v.position[2]);
            }
            d.local_bbox_min = mn; d.local_bbox_max = mx;

            cd::rhi::TextureViewHandle bound_view = white_tex.view;
            if (prim.material_index >= 0 && static_cast<std::size_t>(prim.material_index) < scene.materials.size())
            {
                const auto& mat = scene.materials[static_cast<std::size_t>(prim.material_index)];
                d.base_color = mat.base_color_factor;
                d.metallic   = mat.metallic_factor;
                d.roughness  = mat.roughness_factor;
                if (mat.base_color_texture >= 0 &&
                    static_cast<std::size_t>(mat.base_color_texture) < gpu_textures.size() &&
                    gpu_textures[static_cast<std::size_t>(mat.base_color_texture)].view.is_valid())
                    bound_view = gpu_textures[static_cast<std::size_t>(mat.base_color_texture)].view;
            }

            if (!d.vb.is_valid() || !d.ib.is_valid()) { d.destroy(device); continue; }

            auto inst_r = cd::material::MaterialInstance::create(device, material);
            if (!inst_r.has_value()) { d.destroy(device); continue; }
            d.instance = std::move(*inst_r);

            std::array<cd::rhi::DescriptorWrite, 1> writes {
                cd::rhi::DescriptorWrite {
                    .binding = 0, .array_element = 0,
                    .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                    .buffer = {}, .buffer_offset = 0, .buffer_range = 0,
                    .view = bound_view, .sampler = sampler }
            };
            if (!d.instance.update(writes).has_value()) { d.destroy(device); continue; }
            drawables.push_back(std::move(d));
        }
    }
    if (drawables.empty()) { std::fprintf(stderr, "[gltf] no drawable primitives\n"); return 8; }
    std::printf("[gltf] uploaded %zu primitive(s).\n", drawables.size());
    std::fflush(stdout);

    cd::camera::Camera cam = cd::camera::auto_frame_aabb(scene.bbox_min, scene.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);
    orbit.auto_spin_rate = 0.6F;

    std::printf("[gltf] ready.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0) return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFmt, depth)) return false;
        depth_init = false; needs_rebuild = false;
        return true;
    };

    auto t_prev = std::chrono::steady_clock::now();
    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild()) continue;

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 9;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        const auto t_now = std::chrono::steady_clock::now();
        orbit.update_auto(cam, std::chrono::duration<float>(t_now - t_prev).count());
        t_prev = t_now;

        if (!depth_init)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                    .texture = depth.image,
                    .from = cd::rhi::ResourceState::kUndefined, .to = cd::rhi::ResourceState::kDepthWrite,
                    .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                }
            };
            cmd.barrier({}, dbar);
            depth_init = true;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } } }
        };
        cd::rhi::DepthStencilAttachmentInfo da {};
        da.view = depth.view; da.depth_load = cd::rhi::LoadOp::kClear;
        da.depth_store = cd::rhi::StoreOp::kStore; da.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca; rp.depth_stencil = &da;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const auto vp  = cd::camera::view_projection(cam, aspect);
        const auto eye = cd::camera::world_position(cam);
        const auto frustum = cd::camera::extract_frustum(vp);

        material.apply(cmd);
        for (const auto& inst : scene.instances)
        {
            for (const auto& d : drawables)
            {
                if (d.mesh_index != inst.mesh_index) continue;

                constexpr float kInf = std::numeric_limits<float>::infinity();
                std::array<cd::math::Vec3f, 8> corners {
                    cd::math::Vec3f { d.local_bbox_min[0], d.local_bbox_min[1], d.local_bbox_min[2] },
                    cd::math::Vec3f { d.local_bbox_max[0], d.local_bbox_min[1], d.local_bbox_min[2] },
                    cd::math::Vec3f { d.local_bbox_min[0], d.local_bbox_max[1], d.local_bbox_min[2] },
                    cd::math::Vec3f { d.local_bbox_max[0], d.local_bbox_max[1], d.local_bbox_min[2] },
                    cd::math::Vec3f { d.local_bbox_min[0], d.local_bbox_min[1], d.local_bbox_max[2] },
                    cd::math::Vec3f { d.local_bbox_max[0], d.local_bbox_min[1], d.local_bbox_max[2] },
                    cd::math::Vec3f { d.local_bbox_min[0], d.local_bbox_max[1], d.local_bbox_max[2] },
                    cd::math::Vec3f { d.local_bbox_max[0], d.local_bbox_max[1], d.local_bbox_max[2] },
                };
                cd::math::Vec3f wmn { kInf, kInf, kInf };
                cd::math::Vec3f wmx { -kInf, -kInf, -kInf };
                for (const auto& c : corners)
                {
                    const auto w4 = inst.world_matrix * cd::math::Vec4f { c[0], c[1], c[2], 1.F };
                    wmn[0] = std::min(wmn[0], w4[0]); wmn[1] = std::min(wmn[1], w4[1]); wmn[2] = std::min(wmn[2], w4[2]);
                    wmx[0] = std::max(wmx[0], w4[0]); wmx[1] = std::max(wmx[1], w4[1]); wmx[2] = std::max(wmx[2], w4[2]);
                }
                if (cd::camera::test_aabb(frustum, wmn, wmx) == cd::camera::CullResult::kOutside) continue;

                GltfPushBlock pb {};
                pb.mvp         = vp * inst.world_matrix;
                pb.base_color  = d.base_color;
                pb.mr_pad      = { d.metallic, d.roughness, 0.08F, 0.0F };
                pb.camera_pos  = { eye[0], eye[1], eye[2], 1.0F };
                cmd.push_constants(material.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                    0, static_cast<std::uint32_t>(sizeof(pb)), &pb);
                d.instance.bind(cmd, 0);
                cmd.bind_vertex_buffer(0, d.vb, 0);
                cmd.bind_index_buffer(d.ib, 0, cd::rhi::IndexType::kUInt32);
                cmd.draw_indexed(d.index_count, 1, 0, 0, 0);
            }
        }
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 10;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    for (auto& d : drawables) d.destroy(device);
    for (auto& t : gpu_textures) t.destroy(device);
    white_tex.destroy(device);
    device.destroy_sampler(sampler);
    depth.destroy(device);
    std::printf("[gltf] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// Section F — textured_cooked (ex hello_textured_cooked)
// BC7 compress→.cdtex cook→load→GPU upload roundtrip.
// =============================================================================
namespace
{

[[nodiscard]] std::filesystem::path make_self_cooked_texture()
{
    constexpr std::uint32_t kSz = 256;
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("cd_textured_cooked_" + std::to_string(static_cast<std::uint64_t>(stamp)) +
                 "_" + std::to_string(seq.fetch_add(1)) + ".cdtex");

    // Procedural RGBA
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kSz) * kSz * 4);
    for (std::uint32_t y = 0; y < kSz; ++y)
        for (std::uint32_t x = 0; x < kSz; ++x)
        {
            const float u   = static_cast<float>(x) / static_cast<float>(kSz - 1);
            const float v   = static_cast<float>(y) / static_cast<float>(kSz - 1);
            const bool dark = ((x >> 5) ^ (y >> 5)) & 1u;
            const auto base = dark ? 50U : 200U;
            const auto i    = (y * kSz + x) * 4U;
            px[i + 0] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(u * 50.0F));
            px[i + 1] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(v * 50.0F));
            px[i + 2] = static_cast<std::uint8_t>(base);
            px[i + 3] = 0xFF;
        }

    auto bc7 = cd::asset::image::compress_bc7(px, kSz, kSz, cd::asset::image::Bc7Quality::kBalanced);
    if (!bc7.has_value()) return {};

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return {};

    auto wu32 = [&](std::uint32_t v2) {
        const std::uint8_t b[4] {
            static_cast<std::uint8_t>(v2),
            static_cast<std::uint8_t>(v2 >> 8),
            static_cast<std::uint8_t>(v2 >> 16),
            static_cast<std::uint8_t>(v2 >> 24)
        };
        f.write(reinterpret_cast<const char*>(b), 4);
    };
    auto wu16 = [&](std::uint16_t v2) {
        const std::uint8_t b[2] { static_cast<std::uint8_t>(v2), static_cast<std::uint8_t>(v2 >> 8) };
        f.write(reinterpret_cast<const char*>(b), 2);
    };
    f.write("CDBC7", 5);
    const std::uint8_t ver = 1;
    f.write(reinterpret_cast<const char*>(&ver), 1);
    wu32(kSz); wu32(kSz);
    wu16(static_cast<std::uint16_t>(bc7->block_w));
    wu16(static_cast<std::uint16_t>(bc7->block_h));
    f.write(reinterpret_cast<const char*>(bc7->data.data()),
            static_cast<std::streamsize>(bc7->data.size()));
    if (!f.good()) return {};
    return path;
}

[[nodiscard]] int run_textured_cooked(const cd::sample::Runtime& runtime, const char* cdtex_path)
{
    std::printf("=== [textured_cooked] BC7 .cdtex cook→load→GPU roundtrip ===\n");
    std::fflush(stdout);

    constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 v_uv;
void main() { gl_Position = vec4(in_pos, 0.0, 1.0); v_uv = in_uv; }
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = texture(u_tex, v_uv); }
)glsl";

    struct Vtx { float pos[2]; float uv[2]; };
    constexpr std::array<Vtx, 4> kVerts {{
        { { -0.7F, -0.7F }, { 0.0F, 0.0F } },
        { {  0.7F, -0.7F }, { 1.0F, 0.0F } },
        { {  0.7F,  0.7F }, { 1.0F, 1.0F } },
        { { -0.7F,  0.7F }, { 0.0F, 1.0F } },
    }};
    constexpr std::array<std::uint16_t, 6> kIdx { 0, 1, 2, 0, 2, 3 };

    // Resolve path
    std::string path;
    bool self_cooked = false;
    if (cdtex_path != nullptr)
    {
        path = cdtex_path;
    }
    else
    {
        const auto p = make_self_cooked_texture();
        if (p.empty()) { std::fprintf(stderr, "[textured_cooked] self-cook failed\n"); return 1; }
        path = p.string();
        self_cooked = true;
        std::printf("[textured_cooked] self-cooked %s\n", path.c_str());
    }

    auto tex = cd::asset::cdtex::load(path);
    if (!tex.has_value())
    {
        std::fprintf(stderr, "[textured_cooked] load: %.*s\n",
            static_cast<int>(tex.error().message.size()), tex.error().message.data());
        if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
        return 2;
    }
    std::printf("[textured_cooked] loaded %u x %u BC7 (%u x %u blocks, %zu bytes)\n",
        tex->width, tex->height, tex->block_w, tex->block_h, tex->blocks.size());
    std::fflush(stdout);

    DeviceContext ctx {};
    if (!boot_vulkan("CHROMODYNAMIC — hello_asset_pipeline [textured_cooked]", 800, 800, ctx))
    {
        if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
        return 3;
    }
    auto& window   = *ctx.window;
    auto& device   = *ctx.device;
    auto& renderer = *ctx.renderer;

    // Create BC7 texture + view
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D; td.format = cd::rhi::Format::kBC7Unorm;
    td.extent = { tex->width, tex->height, 1 }; td.mip_levels = 1; td.array_layers = 1;
    td.usage  = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_h = device.create_texture(td);
    if (!tex_h.has_value()) { std::fprintf(stderr, "[textured_cooked] tex create\n"); return 4; }

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = *tex_h; tvd.type = cd::rhi::TextureType::k2D;
    tvd.format = cd::rhi::Format::kBC7Unorm;
    tvd.base_mip = 0; tvd.mip_count = 1; tvd.base_layer = 0; tvd.layer_count = 1;
    auto view_h = device.create_texture_view(tvd);
    if (!view_h.has_value()) { device.destroy_texture(*tex_h); return 5; }

    cd::rhi::SamplerDesc sd {};
    sd.mag_filter = cd::rhi::SamplerFilter::kLinear; sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sd.address_u = sd.address_v = sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_h = device.create_sampler(sd);
    if (!samp_h.has_value()) { return 6; }

    const std::span<const std::byte> blk_bytes {
        reinterpret_cast<const std::byte*>(tex->blocks.data()), tex->blocks.size()
    };
    const auto staging = make_upload_buffer(device, blk_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid()) { return 7; }
    if (!upload_texture_2d(device, *tex_h, staging, tex->width, tex->height)) { return 8; }
    device.destroy_buffer(staging);

    // Geometry
    const std::span<const std::byte> vb_bytes {
        reinterpret_cast<const std::byte*>(kVerts.data()), kVerts.size() * sizeof(Vtx)
    };
    const std::span<const std::byte> ib_bytes {
        reinterpret_cast<const std::byte*>(kIdx.data()), kIdx.size() * sizeof(std::uint16_t)
    };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid()) { return 9; }

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr) { return 10; }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vtx), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRG32Float, offsetof(Vtx, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRG32Float, offsetof(Vtx, uv)  },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFmts { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS; md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings; md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFmts;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "bc7_quad";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value()) { return 11; }
    auto& material = *mat_r;

    auto inst_r = cd::material::MaterialInstance::create(device, material);
    if (!inst_r.has_value()) { return 12; }
    auto& instance = *inst_r;

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite {
            .binding = 0, .array_element = 0,
            .type = cd::rhi::DescriptorType::kCombinedImageSampler,
            .buffer = {}, .buffer_offset = 0, .buffer_range = 0,
            .view = *view_h, .sampler = *samp_h }
    };
    if (!instance.update(writes).has_value()) { return 13; }

    std::printf("[textured_cooked] ready.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(32);
    bool needs_rebuild = false;
    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx)) window.request_close();
        events.clear();
        if (!window.pump_events(events)) break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0) continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value()) continue;
            needs_rebuild = false;
        }

        auto fr = renderer.begin_frame();
        if (!fr.has_value())
        {
            if (fr.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 14;
        }
        auto& frame = *fr;
        auto& cmd   = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> ca {
            cd::rhi::ColorAttachmentInfo {
                .view = frame.swapchain_image_view,
                .load_op = cd::rhi::LoadOp::kClear, .store_op = cd::rhi::StoreOp::kStore,
                .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } } }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
        rp.color_attachments = ca;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(cd::rhi::Viewport { 0.F, 0.F, static_cast<float>(frame.extent.width), static_cast<float>(frame.extent.height), 0.F, 1.F });
        cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });

        material.apply(cmd);
        instance.bind(cmd, 0);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(static_cast<std::uint32_t>(kIdx.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto er = renderer.end_frame();
        if (!er.has_value())
        {
            if (er.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            { needs_rebuild = true; continue; }
            return 15;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib); device.destroy_buffer(vb);
    device.destroy_sampler(*samp_h);
    device.destroy_texture_view(*view_h);
    device.destroy_texture(*tex_h);
    if (self_cooked) { std::error_code ec; std::filesystem::remove(path, ec); }
    std::printf("[textured_cooked] clean exit.\n");
    return 0;
}

}  // namespace

// =============================================================================
// main — dispatch
// =============================================================================
int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // Find section name: first non-flag argv token.
    const char* section = nullptr;
    // Asset path is the token AFTER the section name (if any).
    const char* asset_arg = nullptr;
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a { argv[i] };
            if (a.size() >= 2 && a[0] == '-' && a[1] == '-')
            {
                // skip --headless <N>
                if (a == "--headless" && i + 1 < argc)
                {
                    const char* nxt = argv[i + 1];
                    bool num = (nxt[0] != '\0');
                    for (std::size_t k = 0; nxt[k] != '\0' && num; ++k)
                        num = (nxt[k] >= '0' && nxt[k] <= '9');
                    if (num) ++i;
                }
                continue;
            }
            if (section == nullptr)
            {
                section = argv[i];
            }
            else
            {
                // Second non-flag token → asset path for that section
                asset_arg = argv[i];
                break;
            }
        }
        // If section name looks like a file path (contains '/' or '.' but
        // is not a known section keyword), treat it as no-section + asset path.
        // (This keeps backwards compat with: hello_asset_pipeline path.obj)
        if (section != nullptr)
        {
            const std::string_view sv { section };
            const bool known = (sv == "mesh" || sv == "texture" || sv == "obj" ||
                                 sv == "cooked" || sv == "gltf" || sv == "textured_cooked");
            if (!known)
            {
                // Treat it as asset path for a bare invocation (not standard usage,
                // but guard against accidental mis-dispatch).
                asset_arg = section;
                section   = nullptr;
            }
        }
    }

    // Run a single named section
    auto dispatch_one = [&](const char* name, const char* path) -> int
    {
        const std::string_view sv { name };
        if (sv == "mesh")             return run_mesh(runtime);
        if (sv == "texture")          return run_texture(runtime);
        if (sv == "obj")              return run_obj(runtime, path);
        if (sv == "cooked")           return run_cooked(runtime, path);
        if (sv == "gltf")             return run_gltf(runtime, path);
        if (sv == "textured_cooked")  return run_textured_cooked(runtime, path);
        std::fprintf(stderr, "hello_asset_pipeline: unknown section '%s'\n", name);
        std::fprintf(stderr, "  valid: mesh | texture | obj | cooked | gltf | textured_cooked\n");
        return 1;
    };

    if (section != nullptr)
    {
        // Single-section mode
        return dispatch_one(section, asset_arg);
    }

    // All-sections mode — run in order; stop at first failure
    constexpr std::array<const char*, 6> kAll {
        { "mesh", "texture", "obj", "cooked", "gltf", "textured_cooked" }
    };
    for (const char* name : kAll)
    {
        std::printf("\n");
        const int rc = dispatch_one(name, nullptr);
        if (rc != 0)
        {
            std::fprintf(stderr, "hello_asset_pipeline: section '%s' failed (exit %d)\n", name, rc);
            return rc;
        }
    }

    std::printf("\nhello_asset_pipeline: all sections passed.\n");
    return 0;
}
