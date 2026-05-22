// =============================================================================
// CHROMODYNAMIC — samples/hello_texture/main.cpp
//
// Adds the image pipeline to the engine demo set:
//   * Procedural 256×256 RGBA checker generated at startup (no asset
//     dependency — keeps the demo self-contained).
//   * Staging buffer (host-visible) → vkCmdCopyBufferToImage → device-
//     local sampled image. Exercises the freshly-added engine
//     `ICommandBuffer::copy_buffer_to_image`.
//   * Sampler + combined-image-sampler descriptor set bound to the
//     material via cd::material::MaterialInstance.
//   * Quad with explicit UVs; fragment samples the texture.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{

struct Vertex
{
    float pos[2];
    float uv[2];
};

constexpr std::array<Vertex, 4> kVertices {
    {
     { { -0.7F, -0.7F }, { 0.0F, 0.0F } },
     { { 0.7F, -0.7F }, { 1.0F, 0.0F } },
     { { 0.7F, 0.7F }, { 1.0F, 1.0F } },
     { { -0.7F, 0.7F }, { 0.0F, 1.0F } },
     }
};
constexpr std::array<std::uint16_t, 6> kIndices { 0, 1, 2, 0, 2, 3 };

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 v_uv;
void main() {
  gl_Position = vec4(in_pos, 0.0, 1.0);
  v_uv = in_uv;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = texture(u_tex, v_uv); }
)glsl";

constexpr std::uint32_t kTexSize = 256;

/// Generate a colorful 8×8 checker pattern with mild gradient overlay so
/// the result is visually distinct — proves UV interpolation and texture
/// sampling are wired up.
[[nodiscard]] std::vector<std::uint8_t> make_checker_pixels()
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kTexSize) * kTexSize * 4);
    for (std::uint32_t y = 0; y < kTexSize; ++y)
    {
        for (std::uint32_t x = 0; x < kTexSize; ++x)
        {
            const bool dark = ((x >> 5) ^ (y >> 5)) & 1u;
            const auto u = static_cast<float>(x) / static_cast<float>(kTexSize - 1);
            const auto v = static_cast<float>(y) / static_cast<float>(kTexSize - 1);
            const auto base = dark ? 60U : 200U;
            const auto i = (y * kTexSize + x) * 4U;
            px[i + 0] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(u * 40.0F));
            px[i + 1] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(v * 40.0F));
            px[i + 2] = static_cast<std::uint8_t>(base);
            px[i + 3] = 255U;
        }
    }
    return px;
}

[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice& dev, std::span<const std::byte> bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size = bytes.size();
    bd.usage = usage;
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

/// Submit a one-shot command buffer that uploads texels from a staging
/// buffer into a device-local image. Pipeline:
///   barrier(UNDEFINED → TRANSFER_DST) → copy_buffer_to_image → barrier
///   (TRANSFER_DST → SHADER_RESOURCE) → submit → wait_idle.
/// Caller owns both the staging buffer (destroy after wait_idle) and the
/// destination texture.
[[nodiscard]] bool upload_texture_2d(
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle image,
    cd::rhi::BufferHandle staging,
    std::uint32_t w,
    std::uint32_t h
)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr)
        return false;
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 1> to_dst {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kTransferDst,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_dst);

    std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
                                        .buffer_offset = 0,
                                        .mip_level = 0,
                                        .base_layer = 0,
                                        .layer_count = 1,
                                        .image_offset = { 0, 0, 0 },
                                        .image_extent = { w, h, 1 },
                                        }
    };
    cmd->copy_buffer_to_image(staging, image, regions);

    std::array<cd::rhi::TextureBarrier, 1> to_read {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kTransferDst,
                                 .to = cd::rhi::ResourceState::kShaderResource,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_read);

    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_texture (checker pattern via descriptor set)";
    wd.width = 800;
    wd.height = 800;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "window: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 1;
    }
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(
            stderr,
            "device: %.*s\n",
            static_cast<int>(device_r.error().message.size()),
            device_r.error().message.data()
        );
        return 2;
    }
    auto& device = **device_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(
            stderr,
            "renderer: %.*s\n",
            static_cast<int>(renderer_r.error().message.size()),
            renderer_r.error().message.data()
        );
        return 3;
    }
    auto& renderer = *renderer_r;

    // ---- Texture: create device-local image, upload checker via staging ---
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { kTexSize, kTexSize, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_r = device.create_texture(td);
    if (!tex_r.has_value())
    {
        std::fprintf(
            stderr,
            "texture create: %.*s\n",
            static_cast<int>(tex_r.error().message.size()),
            tex_r.error().message.data()
        );
        return 4;
    }
    const auto texture = *tex_r;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = texture;
    tvd.type = cd::rhi::TextureType::k2D;
    tvd.format = cd::rhi::Format::kRGBA8Unorm;
    tvd.base_mip = 0;
    tvd.mip_count = 1;
    tvd.base_layer = 0;
    tvd.layer_count = 1;
    auto view_r = device.create_texture_view(tvd);
    if (!view_r.has_value())
    {
        std::fprintf(
            stderr,
            "view: %.*s\n",
            static_cast<int>(view_r.error().message.size()),
            view_r.error().message.data()
        );
        return 5;
    }
    const auto tex_view = *view_r;

    // Sampler — linear filter, repeat addressing (UVs are inside [0,1] here
    // but repeat is the safe default for tiled patterns).
    cd::rhi::SamplerDesc sd {};
    sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sd.address_u = cd::rhi::SamplerAddressMode::kRepeat;
    sd.address_v = cd::rhi::SamplerAddressMode::kRepeat;
    sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_r = device.create_sampler(sd);
    if (!samp_r.has_value())
    {
        std::fprintf(
            stderr,
            "sampler: %.*s\n",
            static_cast<int>(samp_r.error().message.size()),
            samp_r.error().message.data()
        );
        return 6;
    }
    const auto sampler = *samp_r;

    // Staging upload.
    const auto pixels = make_checker_pixels();
    const std::span<const std::byte> px_bytes { reinterpret_cast<const std::byte*>(pixels.data()), pixels.size() };
    const auto staging = make_upload_buffer(device, px_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid())
    {
        std::fprintf(stderr, "staging upload\n");
        return 7;
    }
    if (!upload_texture_2d(device, texture, staging, kTexSize, kTexSize))
    {
        std::fprintf(stderr, "upload_texture_2d\n");
        return 8;
    }
    device.destroy_buffer(staging);

    // ---- Geometry ---------------------------------------------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVertices.data()),
                                                kVertices.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);

    // ---- Material with descriptor binding for the combined image-sampler --
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 9;
    }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRG32Float, offsetof(Vertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRG32Float, offsetof(Vertex, uv)  }
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFormats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "textured_quad";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "material: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 10;
    }
    auto& material = *material_r;

    // Allocate the descriptor set and point it at the texture+sampler.
    auto inst_r = cd::material::MaterialInstance::create(device, material);
    if (!inst_r.has_value())
    {
        std::fprintf(
            stderr,
            "instance: %.*s\n",
            static_cast<int>(inst_r.error().message.size()),
            inst_r.error().message.data()
        );
        return 11;
    }
    auto& instance = *inst_r;

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite { .binding = 0,
                                  .array_element = 0,
                                  .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                  .buffer = {},
                                  .buffer_offset = 0,
                                  .buffer_range = 0,
                                  .view = tex_view,
                                  .sampler = sampler }
    };
    if (auto wr = instance.update(writes); !wr.has_value())
    {
        std::fprintf(
            stderr,
            "desc update: %.*s\n",
            static_cast<int>(wr.error().message.size()),
            wr.error().message.data()
        );
        return 12;
    }

    std::printf("hello_texture: ready. ESC or close to exit.\n");
    std::fflush(stdout);

    // ---- Main loop --------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0)
            return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
            return false;
        needs_rebuild = false;
        return true;
    };

    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx))
            window.request_close();
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
            {
                window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
        }
        if (needs_rebuild && !rebuild())
            continue;

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 13;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } },
                                          }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(
            cd::rhi::Viewport { 0.0F,
                                0.0F,
                                static_cast<float>(frame.extent.width),
                                static_cast<float>(frame.extent.height),
                                0.0F,
                                1.0F }
        );
        cmd.set_scissor(
            cd::rhi::Rect2D {
                { 0, 0 },
                frame.extent
        }
        );

        material.apply(cmd);
        instance.bind(cmd, /*set_index=*/0);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(static_cast<std::uint32_t>(kIndices.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 14;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    device.destroy_sampler(sampler);
    device.destroy_texture_view(tex_view);
    device.destroy_texture(texture);
    std::printf("hello_texture: clean exit.\n");
    return 0;
}
